#pragma once

#include "parse_json_value.hpp"

#include <octave/ov.h>
#include <simdjson.h>

#include <atomic>
#include <exception>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

namespace octave_ndjson
{
    class ParseResult
    {
    public:
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

        using Result = std::variant<Parsed, Error>;

        ParseResult(octave_value value, Schema schema, std::string_view string, std::size_t line_number)
            : m_result{ Parsed{ std::move(value), std::move(schema) } }
            , m_info{ string, line_number }
        {
        }

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

        bool is_error() const { return std::holds_alternative<Error>(m_result); }
        bool is_parsed() const { return std::holds_alternative<Parsed>(m_result); }

        Result m_result;
        Info   m_info;
    };

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
            , m_strings(concurrency)
            , m_futures(concurrency)
            , m_parsers(concurrency)
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

        std::optional<ParseResult> parse(std::string_view string, std::size_t line_number)
        {
            auto prev_result = std::optional<ParseResult>{};
            auto index       = m_index;

            m_wake_flag[index].wait(true);

            if (m_error_index == index) {
                prev_result.emplace(
                    m_error.m_exception,
                    m_strings[index].m_string,
                    m_strings[index].m_line_number,
                    m_error.m_offset
                );
                return prev_result;
            }

            if (auto fut = std::exchange(m_futures[index], std::nullopt); fut.has_value()) {
                prev_result.emplace(
                    std::move(fut->m_value),
                    std::move(fut->m_schema),
                    m_strings[index].m_string,
                    m_strings[index].m_line_number
                );
            }

            m_strings[index]   = Input{ string, line_number };
            m_wake_flag[index] = true;
            m_wake_flag[index].notify_one();

            m_index = (index + 1) % m_concurrency;

            return prev_result;
        }

        std::vector<ParseResult> drain()
        {
            auto remaining = std::vector<ParseResult>{};

            for (auto i : sv::iota(0u, m_concurrency)) {
                auto index = (m_index + i) % m_concurrency;
                m_wake_flag[index].wait(true);

                if (m_error_index == index) {
                    remaining.emplace_back(
                        m_error.m_exception,
                        m_strings[index].m_string,
                        m_strings[index].m_line_number,
                        m_error.m_offset
                    );
                }

                if (auto fut = std::exchange(m_futures[index], std::nullopt); fut.has_value()) {
                    remaining.emplace_back(
                        std::move(fut->m_value),
                        std::move(fut->m_schema),
                        m_strings[index].m_string,
                        m_strings[index].m_line_number
                    );
                }
            }

            return remaining;
        }

    private:
        void thread_function(std::stop_token stop_token, std::size_t index)
        {
            auto& parser = m_parsers[index];
            auto& input  = m_strings[index];
            auto& output = m_futures[index];
            auto& flag   = m_wake_flag[index];

            auto buffer = std::string{};

            while (not stop_token.stop_requested()) {
                flag.wait(false);
                if (stop_token.stop_requested()) {
                    break;
                }

                auto string = simdjson::padded_string{ input.m_string };
                auto doc    = simdjson::ondemand::document{};

                try {
                    if (auto err = parser.iterate(string).get(doc); err != simdjson::error_code::SUCCESS) {
                        throw simdjson::simdjson_error{ err };
                    }

                    auto schema = Schema{ 0 };
                    auto value  = parse_json_value(doc.get_value(), schema);

                    output = Parsed{ std::move(value), std::move(schema) };
                } catch (...) {
                    auto idx = std::numeric_limits<std::size_t>::max();
                    if (m_error_index.compare_exchange_strong(idx, index)) {
                        auto offset   = 0l;
                        auto location = doc.current_location();

                        if (not location.error()) {
                            offset = location.value() - string.data();
                        }

                        m_error = ParseResult::Error{ std::current_exception(), static_cast<size_t>(offset) };
                    }
                }

                flag = false;
                flag.notify_one();
            }
        }

        std::size_t m_concurrency;
        std::size_t m_index = 0;

        std::atomic<std::size_t> m_error_index = std::numeric_limits<std::size_t>::max();
        ParseResult::Error       m_error;

        std::vector<std::atomic<bool>>          m_wake_flag;
        std::vector<Input>                      m_strings;    // input
        std::vector<std::optional<Parsed>>      m_futures;    // output
        std::vector<simdjson::ondemand::parser> m_parsers;
        std::vector<std::jthread>               m_threads;
    };
}
