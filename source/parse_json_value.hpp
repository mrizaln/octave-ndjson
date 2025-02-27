#pragma once

#include "schema.hpp"

#include <octave/ov.h>
#include <simdjson.h>

#include <string>
#include <vector>

namespace octave_ndjson
{
    // TODO: optimize this function
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
}
