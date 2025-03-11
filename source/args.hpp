#pragma once

#include <string>

class octave_value_list;

namespace octave_ndjson
{
    // defined in ndjson_load.hpp
    enum class ParseMode;
}

namespace octave_ndjson::args
{
    enum class Threading
    {
        Single,
        Multi,
    };

    enum class Kind
    {
        String,
        File,
    };

    struct ParsedArgs
    {
        std::string m_path_or_string;
        ParseMode   m_mode;
        Threading   m_threading;
    };

    /**
     * @brief Parse octave argument list.
     *
     * @param args The argument list.
     * @param kind Specifies the first argument type kind (filepath or string).
     * @param error_prefix Prefix to be added to error message when error happen.
     *
     * @return ParsedArgs on success.
     *
     * @throw <internal_octave_error> if parsing failed.
     */
    ParsedArgs parse(const octave_value_list& args, Kind kind, const char* error_prefix);
}
