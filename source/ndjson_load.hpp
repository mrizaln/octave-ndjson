#pragma once

// AAAAaaaAAaAAAaAAaAAAaaAaaaaaAAaaaAAAAaaaaa
// - what the heck octave naming convention is so inconsistent!
// - futhermore, why does most of the thing does not live in the octave namespace???????????????
// - i'm having a hard time dealing with naming collisions :((((((((((
// - the fuck with the class hierarchy also??

#include <octave/oct-map.h>
#include <octave/oct.h>
#include <simdjson.h>

#include <format>
#include <string>
#include <vector>

#if defined(NDEBUG)
#    define LOG(...)
#else
#    define LOG(...) std::cout << std::format(__VA_ARGS__) << '\n';
#endif

namespace octave_ndjson
{
    inline std::string escape_whitespace(std::string_view string)
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

    inline octave_value parse_json_value(simdjson::ondemand::value value) noexcept(false)
    {
        using Type = simdjson::ondemand::json_type;

        switch (value.type()) {
        case Type::array: {
            // NOTE: at the moment I'm only handling 1D array

            // TODO: if the array contains other array with the same size then it can be made into actual
            // multidimensional array

            auto array      = std::vector<octave_value>{};
            auto all_number = false;

            for (auto elem : value.get_array()) {
                auto& parsed = array.emplace_back(parse_json_value(elem.value()));
                all_number   = parsed.is_scalar_type() and parsed.isnumeric();
            }

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
            auto map = octave_scalar_map{};

            for (auto field : value.get_object()) {
                auto key   = std::string{ field.unescaped_key().value() };
                auto value = parse_json_value(field.value().value());

                map.setfield(key, value);
            }

            return map;
        }
        case Type::string: {
            // very inefficient; two copies were made here:
            // - first, from simdjson (std::string_view) to std::string
            // - then, from std::string to octave_value
            // given the design of the octave API, I can't do anything about that one, copy is a must there
            // but at the simdjson to std::string part, I can pass a previously created std::string into
            // get_string() method so a buffer that is repeatedly used may be better, this function is
            // recursive though, passing a string buffer repeatedly is kinda a hassle, maybe I will use
            // std::stack instead in the future so I can use iterate approach instead.
            return std::string{ value.get_string().value() };
        }
        case Type::number: return value.get_double().value();
        case Type::boolean: return value.get_bool().value();
        case Type::null: return lo_ieee_na_value();
        default: std::abort();
        }
    }

    inline octave_value load(const std::string& string)
    {
        auto parser = simdjson::ondemand::parser{};
        auto stream = simdjson::ondemand::document_stream{};

        if (auto err = parser.iterate_many(string).get(stream); err) {
            error("failed to initilize simdjson: %s", simdjson::error_message(err));
        }

        auto docs = std::vector<octave_value>{};

        for (auto it = stream.begin(); it != stream.end(); ++it) {
            if (auto err = it.error(); err) {
                error("Got a broken document at %zu: %s", it.current_index(), simdjson::error_message(err));
            }

            auto doc = *it;

            try {
                auto parsed = parse_json_value(doc.get_value().value());
                docs.push_back(std::move(parsed));
            } catch (simdjson::simdjson_error& e) {
                auto offset  = doc.current_location().value() - string.data();
                offset      -= offset > 0;

                auto to_end = string.size() - static_cast<std::size_t>(offset);
                auto substr = escape_whitespace({ string.data() + offset, std::min(to_end, 50ul) });

                auto message = std::format(
                    "parsing error\n"
                    "\t> {}\n\n"
                    "\t> at offset {}: \033[32m{}{}{}\033[00m\n"
                    "\t                     ^\n"
                    "\t                     |\n"
                    "\t        starts from here",
                    e.what(),
                    offset,
                    offset > 0 ? " ... " : "<BOF>",
                    substr,
                    to_end > 50ul ? " ... " : "<EOF>"
                );

                error("%s", message.c_str());
            } catch (std::exception& e) {
                error("Unknown exception: %s", e.what());
            }
        }

        auto cell = Cell{ dim_vector{ 1, static_cast<long>(docs.size()) } };
        for (auto i = 0ul; i < docs.size(); ++i) {
            cell(0, static_cast<long>(i)) = docs[i];
        }

        return octave_value{ cell };
    }
}
