#pragma once

// AAAAaaaAAaAAAaAAaAAAaaAaaaaaAAaaaAAAAaaaaa
// - what the heck octave naming convention is so inconsistent!
// - futhermore, why does most of the thing does not live in the octave namespace???????????????
// - i'm having a hard time dealing with naming collisions :((((((((((
// - what the heck with the class hierarchy also??

#include "multithreaded_parser.hpp"

#include <octave/oct-map.h>
#include <octave/oct.h>
#include <simdjson.h>

#include <format>
#include <string>
#include <vector>

namespace octave_ndjson
{
    enum class ParseMode
    {
        // Documents must have the same schema, including the number of elements and their types on array
        Strict,

        // Documents must have the same schema, but the number of elements and their types on array can vary
        DynamicArray,

        // Documents can have different schemas
        Relaxed,
    };

    struct ParseException : std::runtime_error
    {
        ParseException(
            const char*      message,
            std::string_view string,
            std::size_t      line_number,
            std::size_t      offset
        )
            : std::runtime_error{ message }
            , m_string{ string }
            , m_line_number{ line_number }
            , m_offset{ offset }
        {
        }

        std::string_view m_string;
        std::size_t      m_line_number;
        std::size_t      m_offset;
    };

    /**
     * @brief Helper function to create an exception from `ParseResult::Error`.
     *
     * @param result Error result.
     * @param info Additional error information.
     */
    [[noreturn]] inline void make_exception(ParseResult::Error result, ParseResult::Info info)
    {
        try {
            std::rethrow_exception(result.m_exception);
        } catch (std::exception& e) {
            throw ParseException{ e.what(), info.m_string, info.m_line_number, result.m_offset };
        }
    }

    /**
     * @brief Create a string with escaped whitespace.
     *
     * @param string The string to be escaped.
     *
     * @return An escaped string.
     */
    inline std::string escape_whitespace(std::string_view string) noexcept
    {
        auto escaped = std::string{};
        for (auto ch : string) {
            switch (ch) {
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            case '\r': escaped += "\\r"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\v': escaped += "\\v"; break;
            default: escaped += ch; break;
            }
        }
        return escaped;
    }

    /**
     * @brief Load and parse a JSON string into an Octave value (single-threaded).
     *
     * @param string The input string.
     * @param mode Parse mode.
     *
     * @return The Octave value.
     *
     * @throw <internal_octave_error> if there is an error parsing the JSON string.
     *
     * I believe the `error` function provided by octave throws an exception, but I have no idea what
     * exception it throws. The `mode` enum specifies the strictness of the schema comparison.
     */
    inline octave_value load(const std::string& string, ParseMode mode)
    {
        auto parser = simdjson::ondemand::parser{};
        auto stream = simdjson::ondemand::document_stream{};

        if (auto err = parser.iterate_many(string).get(stream); err) {
            error("failed to initilize simdjson: %s", simdjson::error_message(err));
        }

        auto docs          = std::vector<octave_value>{};
        auto wanted_schema = std::optional<Schema>{};

        const auto same_schema = [&mode](const Schema& reference, const Schema& schema) {
            switch (mode) {
            case ParseMode::Strict: return reference.is_same(schema, false);
            case ParseMode::DynamicArray: return reference.is_same(schema, true);
            case ParseMode::Relaxed: return true;
            default: std::abort();
            }
        };

        for (auto it = stream.begin(); it != stream.end(); ++it) {
            if (auto err = it.error(); err) {
                error("Got a broken document at %zu: %s", it.current_index(), simdjson::error_message(err));
            }

            auto doc = *it;

            try {
                auto value = simdjson::ondemand::value{};
                if (doc.get_value().get(value)) {
                    throw std::runtime_error{ "The root of document must be either an Object or an Array" };
                }

                auto current_schema = Schema{ wanted_schema ? 0 : wanted_schema->size() };
                auto parsed         = parse_json_value(value, current_schema);

                docs.push_back(std::move(parsed));

                if (not wanted_schema.has_value()) {
                    wanted_schema = current_schema;
                }

                if (not same_schema(*wanted_schema, current_schema)) {
                    auto doc_number  = docs.size();
                    auto wanted_str  = wanted_schema->stringify();
                    auto current_str = current_schema.stringify();

                    auto [wanted_diff, current_diff] = util::create_diff(wanted_str, current_str);

                    auto message = std::format(
                        "Mismatched schema, all documents must have the same schema"
                        "\n\nFirst document:\n{0:}\nCurrent document (document number: {2:}):\n{1:}",
                        wanted_diff,
                        current_diff,
                        doc_number
                    );
                    throw std::runtime_error{ message };
                }
            } catch (std::exception& e) {
                const char* current = nullptr;
                if (doc.current_location().get(current)) {
                    current = string.data() + string.size();
                }
                auto offset  = current - string.data();
                offset      -= offset > 0;

                auto to_end = string.size() - static_cast<std::size_t>(offset);
                auto substr = escape_whitespace({ string.data() + offset, std::min(to_end, 50ul) });

                auto message = std::format(
                    "Parsing error\n"
                    "\t> {}\n\n"
                    "\t> around: [{}\033[1;33m{}\033[00m{}] (at offset: {})\n"
                    "\t                ^\n"
                    "\t                |\n"
                    "\t  parsing ends here",
                    e.what(),
                    offset > 0 ? " ... " : "<bof>",
                    substr,
                    to_end > 50ul ? " ... " : "<eof>",
                    offset
                );

                error("%s", message.c_str());
            }
        }

        auto cell = Array<octave_value>{ dim_vector{ 1, static_cast<long>(docs.size()) } };
        for (auto i = 0ul; i < docs.size(); ++i) {
            cell(0, static_cast<long>(i)) = docs[i];
        }

        return octave_value{ cell };
    }

    /**
     * @brief Load and parse a JSON string into an Octave value (multi-threaded).
     *
     * @param string The input string.
     * @param mode Parse mode.
     *
     * @return The Octave value.
     *
     * @throw <internal_octave_error> if there is an error parsing the JSON string.
     *
     * I believe the `error` function provided by octave throws an exception, but I have no idea what
     * exception it throws. The `mode` enum specifies the strictness of the schema comparison.
     */
    inline octave_value load_multi(std::string& string, ParseMode mode)
    {
        // NOTE: to make sure that the string has enough padding for simdjson use. see the explanation at
        //       MultithreadedParser::thread_function
        auto string_unpadded_size = string.size();
        simdjson::pad(string);
        auto string_unpadded = std::string_view{ string.data(), string_unpadded_size };

        auto multithreaded_parser = MultithreadedParser{ std::thread::hardware_concurrency() };
        auto line_splitter        = util::StringSplitter{ string_unpadded, '\n' };

        auto docs          = std::vector<octave_value>{};
        auto wanted_schema = std::optional<Schema>{};

        auto assert_same_schema = [&](const Schema& other, ParseResult::Info info) {
            const auto same_schema = [&mode](const Schema& reference, const Schema& schema) {
                switch (mode) {
                case ParseMode::Strict: return reference.is_same(schema, false);
                case ParseMode::DynamicArray: return reference.is_same(schema, true);
                case ParseMode::Relaxed: return true;
                default: std::abort();
                }
            };

            if (not same_schema(*wanted_schema, other)) {
                auto wanted_str  = wanted_schema->stringify();
                auto current_str = other.stringify();

                auto [wanted_diff, current_diff] = util::create_diff(wanted_str, current_str);

                auto message = std::format(
                    "Mismatched schema, all documents must have same schema (dynamic_array: {3:})"
                    "\n\nFirst document:\n{0:}\nCurrent document (line: {2:}):\n{1:}",
                    wanted_diff,
                    current_diff,
                    info.m_line_number,
                    mode == ParseMode::DynamicArray
                );

                throw ParseException{ message.c_str(), info.m_string, info.m_line_number, 0 };
            }
        };

        try {
            auto line_number = 0ul;
            while (auto line = line_splitter.next()) {
                ++line_number;

                auto parsed = multithreaded_parser.parse(*line, line_number);

                // at the start of the parsing the parser does not have any previous result, but if the
                // concurrency limit is reached then the parser will return the previous result
                if (parsed.is_empty()) {
                    continue;
                }

                if (parsed.is_error()) {
                    auto error = std::get<ParseResult::Error>(parsed.m_result);
                    make_exception(error, parsed.m_info);
                }

                auto& result               = std::get<ParseResult::Parsed>(parsed.m_result);
                auto&& [oct_value, schema] = std::move(result);

                if (not wanted_schema.has_value()) {
                    wanted_schema = schema;
                }

                assert_same_schema(schema, parsed.m_info);
                docs.push_back(std::move(oct_value));
            }

            // line parsing ends but the parser might still parsing
            for (auto&& parsed : multithreaded_parser.drain()) {
                if (parsed.is_error()) {
                    auto error = std::get<ParseResult::Error>(parsed.m_result);
                    make_exception(error, parsed.m_info);
                }

                auto& result               = std::get<ParseResult::Parsed>(parsed.m_result);
                auto&& [oct_value, schema] = std::move(result);

                if (not wanted_schema.has_value()) {
                    wanted_schema = schema;
                }

                assert_same_schema(schema, parsed.m_info);
                docs.push_back(std::move(oct_value));
            }
        } catch (ParseException& e) {
            auto to_end = e.m_string.size() - e.m_offset;
            auto substr = escape_whitespace({ e.m_string.data() + e.m_offset, std::min(to_end, 50ul) });

            auto message = std::format(
                "Parsing error\n"
                "\t> {}\n\n"
                "\t> around: [{}\033[1;33m{}\033[00m{}] (line: {} | offset: {})\n"
                "\t                ^\n"
                "\t                |\n"
                "\t  parsing ends here",
                e.what(),
                e.m_offset > 0 ? " ... " : "<bol>",
                substr,
                to_end > 50ul ? " ... " : "<eol>",
                e.m_line_number,
                e.m_offset
            );

            error("%s", message.c_str());
        }

        auto cell = Array<octave_value>{ dim_vector{ 1, static_cast<long>(docs.size()) } };
        for (auto i = 0ul; i < docs.size(); ++i) {
            cell(0, static_cast<long>(i)) = docs[i];
        }

        return octave_value{ cell };
    }
}
