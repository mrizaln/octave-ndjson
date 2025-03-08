#pragma once

#include "schema.hpp"

#include <octave/ov.h>
#include <simdjson.h>

#include <atomic>
#include <exception>
#include <thread>
#include <vector>

namespace octave_ndjson
{
    /**
     * @brief Parse a JSON value into an Octave value.
     *
     * @param value The JSON value to be parsed.
     * @param schema Schema object to be filled with the schema of the JSON value.
     *
     * @return The parsed Octave value.
     *
     * @throw simdjson::simdjson_error if there is an error parsing the JSON value.
     *
     * @todo optimize this function
     */
    inline octave_value parse_json_value(
        simdjson::ondemand::value value,
        octave_ndjson::Schema&    schema
    ) noexcept(false)
    {
        using Type = simdjson::ondemand::json_type;

        switch (value.type()) {
        case Type::array: {
            schema.push(Schema::Array::Begin);

            // NOTE: at the moment I'm only handling 1D array
            // TODO: if the array contains other array with the same size then it can be made into actual
            //       multidimensional array

            auto array      = std::vector<octave_value>{};
            auto all_number = true;

            for (auto elem : value.get_array()) {
                auto& parsed  = array.emplace_back(parse_json_value(elem.value(), schema));
                all_number   &= parsed.isnumeric() and parsed.is_scalar_type();
            }

            schema.push(Schema::Array::End);

            if (all_number) {
                auto ndarray = NDArray{ dim_vector{ 1, static_cast<long>(array.size()) } };
                for (auto i = 0ul; i < array.size(); ++i) {
                    ndarray(0, static_cast<long>(i)) = array[i].double_value();
                }
                return ndarray;
            } else {
                auto cell = Cell{ dim_vector{ 1, static_cast<long>(array.size()) } };
                for (auto i = 0ul; i < array.size(); ++i) {
                    cell(0, static_cast<long>(i)) = array[i];
                }
                return cell;
            }
        }
        case Type::object: {
            schema.push(Schema::Object::Begin);

            auto map = octave_scalar_map{};

            for (auto field : value.get_object()) {
                auto key = std::string{ field.unescaped_key().value() };
                schema.push(Schema::Key{ key });

                auto value = parse_json_value(field.value().value(), schema);
                map.setfield(key, value);
            }

            schema.push(Schema::Object::End);

            return map;
        }
        case Type::string:
            schema.push(Schema::Scalar::String);
            return std::string{ value.get_string().value() };

        case Type::number:    //
            schema.push(Schema::Scalar::Number);
            return value.get_double().value();

        case Type::boolean:    //
            schema.push(Schema::Scalar::Bool);
            return value.get_bool().value();

        case Type::null:    //
            schema.push(Schema::Scalar::Null);
            return lo_ieee_na_value();

        default: [[unlikely]] std::abort();
        }
    }

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
        std::vector<Input>             m_inputs;
        std::vector<ParseResult>       m_futures;
        std::vector<std::jthread>      m_threads;
    };
}
