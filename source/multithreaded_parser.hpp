#pragma once

#include "parse_octave_value.hpp"
#include "schema.hpp"

#include <octave/ov.h>
#include <simdjson/dom.h>

#include <atomic>
#include <exception>
#include <thread>
#include <vector>

namespace octave_ndjson
{
    /**
     * @class ParseResult
     *
     * @brief Result of the parsing.
     *
     * Use `std::get` or `std::get_if` on the `ParseResult::m_result` to get the actual result.
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
        ParseResult() noexcept
            : m_result{ std::monostate{} }
            , m_info{ "", 0 }
        {
        }

        /**
         * @brief Construct a `ParseResult` with a parsed value.
         *
         * @param value The parsed value.
         * @param schema The schema of the parsed json.
         * @param string The originial json string.
         * @param line_number The line number of the json string.
         */
        ParseResult(
            octave_value     value,
            Schema           schema,
            std::string_view string,
            std::size_t      line_number
        ) noexcept
            : m_result{ Parsed{ std::move(value), std::move(schema) } }
            , m_info{ string, line_number }
        {
        }

        /**
         * @brief Construct a `ParseResult` with an error.
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
        ) noexcept
            : m_result{ Error{ exception, offset } }
            , m_info{ string, line_number }
        {
        }

        bool is_empty() const noexcept { return std::holds_alternative<std::monostate>(m_result); }
        bool is_error() const noexcept { return std::holds_alternative<Error>(m_result); }
        bool is_parsed() const noexcept { return std::holds_alternative<Parsed>(m_result); }

        Result m_result;
        Info   m_info;
    };

    /**
     * @class MultithreadedParser
     *
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

        /**
         * @brief Construct a `MultithreadedParser` with a given concurrency.
         *
         * @param concurrency Number of threads to be used.
         */
        MultithreadedParser(std::size_t concurrency) noexcept
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
         *
         * @return The result of the parsing.
         *
         * The async scheduling used is a simple round-robin scheduling, thus the returned result might be
         * empty (check with `ParseResult::is_empty` function) at the start of the parsing. The result is
         * guaranteed to be non-empty after the first non-empty result is returned.
         */
        ParseResult parse(std::string_view string, std::size_t line_number) noexcept
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
         * not been retrieved after you done calling `MultithreadedParser::parse` repeatedly. This function
         * will retrieve all the remaining results from the parser.
         */
        std::vector<ParseResult> drain() noexcept
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
        /**
         * @brief The thread function.
         *
         * @param stop_token Stop token.
         * @param index Index of the thread.
         *
         * This function is where all thread work is done. It will parse the input from
         * `MultithreadedParser::m_inputs` and store the result into `MultithreadedParser::m_futures`. Each
         * thread have an index that specifies which input to use and which output to store the result. To
         * avoid data race, the thread will wait for its `m_wake_flag` to be true before doing any work (the
         * flag is set to true on `MultithreadedParser::parse` and `MultithreadedParser::drain` function) and
         * will set the flag to false after the work is done.
         *
         * Why use `std::atomic` for the flag? It's faster and easier to work with instead of using
         * `std::mutex` and `std::condition_variable` combo.
         */
        void thread_function(std::stop_token stop_token, std::size_t index) noexcept
        {
            auto& [string, line_number] = m_inputs[index];

            auto& output = m_futures[index];
            auto& flag   = m_wake_flag[index];

            auto parser = simdjson::dom::parser{};

            while (not stop_token.stop_requested()) {
                flag.wait(false);
                if (stop_token.stop_requested()) {
                    break;
                }

                try {
                    auto schema = Schema{ 0 };
                    auto doc    = parser.parse(string.data(), string.size(), false);
                    auto value  = parse_octave_value(doc.value());

                    output = ParseResult{ std::move(value), std::move(schema), string, line_number };
                } catch (...) {
                    auto offset = 0ull;
                    output      = ParseResult{ std::current_exception(), string, line_number, offset };
                }

                flag = false;
                flag.notify_one();
            }
        }

        std::size_t m_concurrency;
        std::size_t m_index = 0;

        std::vector<std::atomic<bool>> m_wake_flag;
        std::vector<Input>             m_inputs;
        std::vector<ParseResult>       m_futures;
        std::vector<std::jthread>      m_threads;
    };
}
