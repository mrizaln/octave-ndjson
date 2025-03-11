#pragma once

#include <simdjson/dom/element.h>

class octave_value;

namespace octave_ndjson
{
    /**
     * @brief Parse a simdjson dom into an octave_value.
     *
     * @param dom The simdjson dom element.
     *
     * @return A parsed octave_value.
     *
     * @throw simdjson::simdjson_error on parsing error.
     */
    octave_value parse_octave_value(simdjson::dom::element dom);
}
