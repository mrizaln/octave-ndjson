#pragma once

// AAAAaaaAAaAAAaAAaAAAaaAaaaaaAAaaaAAAAaaaaa
// - what the heck octave naming convention is so inconsistent!
// - futhermore, why does most of the thing does not live in the octave namespace???????????????
// - i'm having a hard time dealing with naming collisions :((((((((((
// - what the heck with the class hierarchy also??

#include "schema.hpp"
#include "util.hpp"

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
            // multidimensional array

            auto array      = std::vector<octave_value>{};
            auto all_number = false;

            for (auto elem : value.get_array()) {
                auto& parsed = array.emplace_back(parse_json_value(elem.value(), schema));
                all_number   = parsed.is_scalar_type() and parsed.isnumeric();
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

    inline octave_value load(const std::string& string)
    {
        auto parser = simdjson::ondemand::parser{};
        auto stream = simdjson::ondemand::document_stream{};

        if (auto err = parser.iterate_many(string).get(stream); err) {
            error("failed to initilize simdjson: %s", simdjson::error_message(err));
        }

        auto docs          = std::vector<octave_value>{};
        auto wanted_schema = std::optional<Schema>{};

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

                if (wanted_schema != (current_schema)) {
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
                auto offset  = doc.current_location().value() - string.data();
                offset      -= offset > 0;

                auto to_end = string.size() - static_cast<std::size_t>(offset);
                auto substr = escape_whitespace({ string.data() + offset, std::min(to_end, 50ul) });

                auto message = std::format(
                    "Parsing error\n"
                    "\t> {}\n\n"
                    "\t> at offset {}: \033[1;33m{}{}{}\033[00m\n"
                    "\t                      ^\n"
                    "\t                      |\n"
                    "\t        parsing ends here",
                    e.what(),
                    offset,
                    offset > 0 ? " ... " : "<BOF>",
                    substr,
                    to_end > 50ul ? " ... " : "<EOF>"
                );

                error("%s", message.c_str());
            }
        }

        auto cell = Cell{ dim_vector{ 1, static_cast<long>(docs.size()) } };
        for (auto i = 0ul; i < docs.size(); ++i) {
            cell(0, static_cast<long>(i)) = docs[i];
        }

        return octave_value{ cell };
    }
}
