#pragma once

#include "parse_json_value.hpp"

#include <octave/ov.h>
#include <simdjson.h>

#include <atomic>
#include <exception>
#include <thread>
#include <vector>

namespace octave_ndjson
{
    /**
     * @class ParseResult
     * @brief Result of the parsing.
     *
     * Use std::get or std::get_if on the ParseResult::m_result to get the actual result.
     */
    struct ParseResult
    {
        struct Parsed
        {
            octave_value m_value;
            Schema       m_schema;
        };

        struct Error
        {
            std::exception_ptr m_exception;
            std::size_t        m_offset;
        };

        struct Info
        {
            std::string_view m_string;
            std::size_t      m_line_number;
        };

        using Result = std::variant<std::monostate, Parsed, Error>;

        /**
         * @brief Construct an empty ParseResult.
         */
        ParseResult()
            : m_result{ std::monostate{} }
            , m_info{ "", 0 }
        {
        }

        /**
         * @brief Construct a ParseResult with a parsed value.
         *
         * @param value The parsed value.
         * @param schema The schema of the parsed json.
         * @param string The originial json string.
         * @param line_number The line number of the json string.
         */
        ParseResult(octave_value value, Schema schema, std::string_view string, std::size_t line_number)
            : m_result{ Parsed{ std::move(value), std::move(schema) } }
            , m_info{ string, line_number }
        {
        }

        /**
         * @brief Construct a ParseResult with an error.
         *
         * @param exception The exception that caused the error.
         * @param string The originial json string.
         * @param line_number The line number of the json string.
         * @param offset The offset of the error in the json string.
         */
        ParseResult(
            std::exception_ptr exception,
            std::string_view   string,
            std::size_t        line_number,
            std::size_t        offset
        )
            : m_result{ Error{ exception, offset } }
            , m_info{ string, line_number }
        {
        }

        bool is_empty() const { return std::holds_alternative<std::monostate>(m_result); }
        bool is_error() const { return std::holds_alternative<Error>(m_result); }
        bool is_parsed() const { return std::holds_alternative<Parsed>(m_result); }

        Result m_result;
        Info   m_info;
    };

    /**
     * @class MultithreadedParser
     * @brief Multithreaded JSON parser with round-robin scheduling.
     */
    class MultithreadedParser
    {
    public:
        struct Parsed
        {
            octave_value m_value;
            Schema       m_schema;
        };

        struct Input
        {
            std::string_view m_string;
            std::size_t      m_line_number = 0;
        };

        MultithreadedParser(std::size_t concurrency)
            : m_concurrency{ concurrency }
            , m_index{ 0 }
            , m_wake_flag(concurrency)
            , m_inputs(concurrency)
            , m_futures(concurrency)
        {
            m_threads.reserve(concurrency);
            for (auto i : sv::iota(0u, concurrency)) {
                m_threads.emplace_back([this, i](std::stop_token stop_token) {
                    thread_function(stop_token, i);
                });
            }
        }

        ~MultithreadedParser()
        {
            for (auto i : sv::iota(0u, m_concurrency)) {
                m_threads[i].request_stop();
                m_wake_flag[i] = true;
                m_wake_flag[i].notify_one();
            }
        }

        /**
         * @brief Parse a json string asynchronously.
         *
         * @param string The input string.
         * @param line_number The line number of the input string.
         * @return The result of the parsing.
         *
         * The async scheduling used is a simple round-robin scheduling, thus the returned result might be
         * empty (check with ParseResult::is_empty function) at the start of the parsing. The result is
         * guaranteed to be non-empty after the first non-empty result is returned.
         */
        ParseResult parse(std::string_view string, std::size_t line_number)
        {
            auto index = m_index;
            m_wake_flag[index].wait(true);

            // result must be retrieved before notifying the thread
            auto prev_result = std::exchange(m_futures[index], {});

            m_inputs[index]    = Input{ string, line_number };
            m_wake_flag[index] = true;
            m_wake_flag[index].notify_one();

            m_index = (index + 1) % m_concurrency;

            return prev_result;
        }

        /**
         * @brief Drain the remaining results from the parser.
         *
         * @return The remaining results (guaranteed to be non-empty).
         *
         * Since the parser uses a round-robin scheduling, the parser might still have some results that has
         * not been retrieved after you done calling ParseResult::parse repeatedly. This function will
         * retrieve all the remaining results from the parser.
         */
        std::vector<ParseResult> drain()
        {
            auto remaining = std::vector<ParseResult>{};

            for (auto i : sv::iota(0u, m_concurrency)) {
                auto index = (m_index + i) % m_concurrency;
                m_wake_flag[index].wait(true);

                if (auto result = std::exchange(m_futures[index], {}); not result.is_empty()) {
                    remaining.push_back(std::move(result));
                }
            }

            return remaining;
        }

    private:
        void thread_function(std::stop_token stop_token, std::size_t index)
        {
            auto& [string, line_number] = m_inputs[index];

            auto& output = m_futures[index];
            auto& flag   = m_wake_flag[index];

            auto parser = simdjson::ondemand::parser{};

            while (not stop_token.stop_requested()) {
                flag.wait(false);
                if (stop_token.stop_requested()) {
                    break;
                }

                auto doc = simdjson::ondemand::document{};
                try {
                    // NOTE: this assume that the input string is properly padded even if it is a string_view.
                    //       if the string view is part of a larger string and the string_view points to the
                    //       middle of that string then this should be safe provided that the large string
                    //       have enough padding at the end of it. this means that the only padding required
                    //       is in the end of the original string.
                    doc = parser.iterate(string, string.size() + simdjson::SIMDJSON_PADDING);

                    auto schema = Schema{ 0 };
                    auto value  = parse_json_value(doc.get_value(), schema);

                    output = ParseResult{ std::move(value), std::move(schema), string, line_number };
                } catch (...) {
                    auto offset = 0ull;
                    if (auto location = doc.current_location(); not location.error()) {
                        offset = static_cast<std::size_t>(location.value() - string.data());
                    }
                    output = ParseResult{ std::current_exception(), string, line_number, offset };
                }

                flag = false;
                flag.notify_one();
            }
        }

        std::size_t m_concurrency;
        std::size_t m_index = 0;

        std::vector<std::atomic<bool>> m_wake_flag;
        std::vector<Input>             m_inputs;     // input
        std::vector<ParseResult>       m_futures;    // output
        std::vector<std::jthread>      m_threads;
    };
}
