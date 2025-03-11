#pragma once

#include <simdjson/padded_string_view.h>

class octave_value;

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
    octave_value load(simdjson::padded_string_view string, ParseMode mode);

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
    octave_value load_multi(simdjson::padded_string_view string, ParseMode mode);
}
