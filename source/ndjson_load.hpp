#pragma once

// AAAAaaaAAaAAAaAAaAAAaaAaaaaaAAaaaAAAAaaaaa
// - what the heck octave naming convention is so inconsistent!
// - futhermore, why does most of the thing does not live in the octave namespace???????????????
// - i'm having a hard time dealing with naming collisions :((((((((((
// - what the heck with the class hierarchy also??

#include "parse_octave_value.hpp"
#include "schema.hpp"

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
        auto parser = simdjson::dom::parser{};
        auto stream = simdjson::dom::document_stream{};

        if (auto err = parser.parse_many(string).get(stream); err) {
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
            auto doc = *it;

            try {
                auto value = simdjson::dom::element{};
                if (doc.get(value)) {
                    throw std::runtime_error{ "The root of document must be either an Object or an Array" };
                }

                auto current_schema = Schema{ wanted_schema ? 0 : wanted_schema->size() };
                auto parsed         = parse_octave_value(value);

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
                auto offset = it.current_index();
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
        // NOTE: to make sure that the string has enough padding for simdjson use
        auto string_unpadded_size = string.size();
        simdjson::pad(string);
        auto string_unpadded = std::string_view{ string.data(), string_unpadded_size };

        auto lines  = util::split(string_unpadded, '\n');
        auto result = std::vector<octave_value>{};

        static constexpr auto no_exception = std::numeric_limits<std::size_t>::max();

        auto output          = Cell{ dim_vector(static_cast<long>(lines.size()), 1) };
        auto exception       = std::exception_ptr{};
        auto exception_index = std::atomic<std::size_t>{ no_exception };

        auto parse_function = [&](std::span<const std::string_view> block, long offset) {
            auto parser = simdjson::dom::parser{};
            for (auto i = 0u; auto line : block) {
                if (exception_index != std::numeric_limits<std::size_t>::max()) {
                    break;
                }

                try {
                    auto doc           = parser.parse(line.data(), line.size(), false);
                    output(offset + i) = parse_octave_value(doc.value());
                } catch (...) {
                    auto idx = static_cast<std::size_t>(offset) + i;
                    if (auto i = no_exception; exception_index.compare_exchange_strong(i, idx)) {
                        exception = std::current_exception();
                    }
                }
                ++i;
            }
        };

        try {
            auto concurrency = std::min((std::size_t)std::thread::hardware_concurrency(), lines.size());
            auto block_size  = lines.size() / concurrency;
            auto threads     = std::vector<std::jthread>{};

            for (auto i : sv::iota(0u, concurrency)) {
                auto offset       = i * block_size;
                auto current_size = std::min(block_size, lines.size() - offset);
                auto block        = std::span{ lines.begin() + static_cast<long>(offset), current_size };
                threads.emplace_back(parse_function, block, offset);
            }

            for (auto& thread : threads) {
                thread.join();
            }

            if (exception_index != no_exception) {
                std::rethrow_exception(exception);
            }
        } catch (std::exception& e) {
            auto line   = lines[exception_index];
            auto substr = escape_whitespace(line.substr(0, std::min(line.size(), 50ul)));

            auto message = std::format(
                "Parsing error\n"
                "\t> {}\n\n"
                "\t> around: [<bol>\033[1;33m{}\033[00m{}] (line: {})\n"
                "\t                ^\n"
                "\t                |\n"
                "\t  parsing ends here",
                e.what(),
                substr,
                line.size() > 50ul ? " ... " : "<eol>",
                exception_index + 1
            );

            error("%s", message.c_str());
        }

        return output;
    }
}
