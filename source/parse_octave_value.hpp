#pragma once

#include <octave/Cell.h>
#include <octave/error.h>
#include <octave/oct-map.h>
#include <octave/ov.h>
#include <simdjson.h>

#include <ranges>

namespace octave_ndjson
{
    // code adapted from the octave `jsondecode.cc` source file:
    // https://docs.octave.org/doxygen/dev/d7/d53/jsondecode_8cc_source.html
    namespace detail_parse
    {
        namespace sv = std::views;

        octave_value decode(simdjson::dom::element elem);

        inline octave_value decode_string(std::string_view string)
        {
            // string might be not null terminated, it needs to be copied somewhere
            thread_local static std::string stringbuf = "";

            stringbuf = string;
            return stringbuf;
        }

        inline octave_value decode_numeric_array(simdjson::dom::array array)
        {
            auto ndarray = NDArray{ dim_vector(static_cast<long>(array.size()), 1) };
            for (auto index = 0; auto elem : array) {
                ndarray(index++) = elem.is_null() ? octave_NaN : decode(elem).double_value();
            }
            return ndarray;
        }

        inline octave_value decode_string_and_mixed_array(simdjson::dom::array array)
        {
            auto cell = Cell{ dim_vector(static_cast<long>(array.size()), 1) };
            for (auto index = 0; auto elem : array) {
                cell(index++) = decode(elem);
            }
            return cell;
        }

        inline octave_value decode_boolean_array(simdjson::dom::array array)
        {
            auto ndarray = boolNDArray{ dim_vector(static_cast<long>(array.size()), 1) };
            for (auto index = 0; auto elem : array) {
                ndarray(index++) = elem.get_bool().value();
            }
            return ndarray;
        }

        inline octave_value decode_array_of_arrays(simdjson::dom::array array)
        {
            auto cell = decode_string_and_mixed_array(array).cell_value();

            // only arrays with sub-arrays of booleans and others as cell arrays
            auto is_bool        = cell(0).is_bool_matrix();
            auto is_struct      = cell(0).isstruct();
            auto subarray_dims  = cell(0).dims();
            auto subarray_ndims = cell(0).ndims();
            auto cell_numel     = cell.numel();
            auto field_names    = is_struct ? cell(0).map_value().fieldnames() : string_vector{};

            for (auto i : sv::iota(0l, cell_numel)) {
                // if one element is cell return the cell array as at least one of the subarrays area
                // either an array of strins, objects, or mixed array
                if (cell(i).iscell()) {
                    return cell;
                }
                // if not the same dim of elements or dim == 0, return cell array
                if (cell(i).dims() != subarray_dims or subarray_dims == dim_vector{}) {
                    return cell;
                }
                // if not numeric subarrays only or bool subarrays only, return cell array
                if (cell(i).is_bool_matrix() != is_bool) {
                    return cell;
                }
                // if not struct arrays only, return cell array
                if (cell(i).isstruct() != is_struct) {
                    return cell;
                }
                // if struct arrays have different fields, return cell array
                if (is_struct and (field_names.std_list() != cell(i).map_value().fieldnames().std_list())) {
                    return cell;
                }
            }

            // calculate the dims of the output array
            auto array_dims = dim_vector{};
            array_dims.resize(subarray_ndims + 1);

            array_dims(0) = cell_numel;
            for (auto i : sv::iota(1, subarray_ndims + 1)) {
                array_dims(i) = subarray_dims(i - 1);
            }

            // struct array
            if (is_struct) {
                auto struct_array = octave_map{};
                array_dims.chop_trailing_singletons();

                if (field_names.numel() != 0) {
                    auto value          = Cell{ array_dims };
                    auto subarray_numel = subarray_dims.numel();

                    for (auto j : sv::iota(0l, field_names.numel())) {
                        // populate the array with specific order to generate MATLAB-identical output
                        for (auto k : sv::iota(0l, cell_numel)) {
                            auto subarray_value = cell(k).map_value().getfield(field_names(j));
                            for (auto i : sv::iota(0l, subarray_numel)) {
                                value(k + i * cell_numel) = subarray_value(i);
                            }
                        }
                        struct_array.assign(field_names(j), value);
                    }
                } else {
                    struct_array.resize(array_dims, true);
                }

                return struct_array;
            }

            // numeric array
            auto array_of_array = NDArray{ array_dims };

            // populate the array with specific order to generate MATLAB-identical output
            auto subarray_numel = array_of_array.numel() / cell_numel;
            for (auto k : sv::iota(0l, cell_numel)) {
                auto subarray_value = cell(k).array_value();
                for (auto i : sv::iota(0l, subarray_numel)) {
                    array_of_array(k + i * cell_numel) = subarray_value(i);
                }
            }

            if (is_bool) {
                return boolNDArray{ array_of_array };
            }

            return array_of_array;
        }

        inline octave_value decode_object_array(simdjson::dom::array array)
        {
            auto struct_cell = decode_string_and_mixed_array(array).cell_value();
            auto field_names = struct_cell(0).scalar_map_value().fieldnames();

            auto same_field_names = true;
            for (auto i : sv::iota(1l, struct_cell.numel())) {
                auto current_field_names = struct_cell(i).scalar_map_value().fieldnames();
                if (field_names.std_list() != current_field_names.std_list()) {
                    same_field_names = false;
                    break;
                }
            }

            if (not same_field_names) {
                return struct_cell;
            }

            auto struct_array      = octave_map{};
            auto struct_array_dims = dim_vector{ struct_cell.numel(), 1 };

            if (field_names.numel()) {
                auto value = Cell{ struct_array_dims };
                for (auto i : sv::iota(0l, field_names.numel())) {
                    for (auto k : sv::iota(0l, struct_cell.numel())) {
                        value(k) = struct_cell(k).scalar_map_value().getfield(field_names(i));
                    }
                    struct_array.assign(field_names(i), value);
                }
            } else {
                struct_array.resize(struct_array_dims, true);
            }

            return struct_array;
        }

        inline octave_value decode_array(simdjson::dom::array array)
        {
            using Type = simdjson::dom::element_type;

            if (array.size() == 0) {
                return NDArray{};
            }

            auto same_type  = true;
            auto is_numeric = true;
            auto last_type  = array.at(0).value().type();

            for (auto elem : array) {
                is_numeric &= elem.is_number() or elem.is_null();
                same_type  &= elem.type() == last_type;
            }

            if (is_numeric) {
                return decode_numeric_array(array);
            }

            if (not same_type or last_type == Type::STRING) {
                return decode_string_and_mixed_array(array);
            }

            if (last_type == Type::BOOL) {
                return decode_boolean_array(array);
            } else if (last_type == Type::OBJECT) {
                return decode_object_array(array);
            } else if (last_type == Type::ARRAY) {
                return decode_array_of_arrays(array);
            }

            error("Unidentified type");
        }

        inline octave_value decode_object(simdjson::dom::object object)
        {
            auto map       = octave_scalar_map{};
            auto stringbuf = std::string{};

            for (auto [key, value] : object) {
                stringbuf = key;
                map.assign(stringbuf, decode(value));
            }

            return map;
        }

        inline octave_value decode(simdjson::dom::element dom)
        {
            using T = simdjson::dom::element_type;
            switch (dom.type()) {
            case T::ARRAY: return detail_parse::decode_array(dom.get_array());
            case T::OBJECT: return detail_parse::decode_object(dom.get_object());
            case T::INT64: return dom.get_int64().value();
            case T::UINT64: return dom.get_uint64().value();
            case T::DOUBLE: return dom.get_double().value();
            case T::STRING: return decode_string(dom.get_string().value());
            case T::BOOL: return dom.get_bool().value();
            case T::NULL_VALUE: return NDArray{};
            default: [[unlikely]] std::abort();
            }
        }
    }

    /**
     * @brief Parse a simdjson dom into an octave_value.
     *
     * @param dom The simdjson dom element.
     *
     * @return A parsed octave_value.
     *
     * @throw simdjson::simdjson_error on parsing error.
     */
    inline octave_value parse_octave_value(simdjson::dom::element dom)
    {
        return detail_parse::decode(dom);
    }
}
