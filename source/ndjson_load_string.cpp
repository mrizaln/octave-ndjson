#include "ndjson_load.hpp"

DEFUN_DLD(ndjson_load_string, args, , "ndjson_load_string(<ndjson_str>)")
{
    if (args.length() != 1) {
        octave::print_usage();
    }

    auto json = args(0).string_value();
    return octave_ndjson::load(json);
}
