#include "ndjson_load.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static constexpr auto help_string = R"(
=============================== ndjson_load_file help page ===============================
signature:
    ndjson_load_file(filepath: string, dynamic_array: bool)

parameters:
    > filepath     : Must be a string that points to a file.
    > dynamic_array: This bool flag signals the parser to parse (and validate)
                     arrays as dynamicallly sized array. This flag also turn off
                     the type check on the arrays elements.

behavior:
    By default the [ndjson_load_file] function will parse NDJSON/JSON Lines
    ([jsonl] from hereon) in strict mode i.e. all the documents on the [jsonl]
    must have the same JSON structure from the number of elements of an array,
    the type of each element, type type of object value, to the order of the
    occurence of the key in the document.

example:
    For example, a [data.jsonl] file with content:
    ```
        [1, 2, 3, 4]
        [5, 6, 7]
    ```
    if parsed will return an error with message:

    ```
        octave> ndjson_load_file('data.jsonl')

        error: Parsing error
            > Mismatched schema, all documents must have the same schema

        First document:
        [
            <number> x 4,
        ],

        Current document (document number: 2):
        [
            <number> x 3,
        ],


            > around: [ ... ]<EOF>] (at offset: 21)
                            ^
                            |
              parsing ends here
    ```

    The [dynamic_array] flag is used to relax that restriction.
==========================================================================================
)";

std::string build_error_with_help(std::string_view error)
{
    return std::format("{}\n{}", help_string, error);
}

DEFUN_DLD(ndjson_load_file, args, , "ndjson_load_file(filepath: string, dynamic_array: bool)")
{
    if (args.length() != 2) {
        error("%s", build_error_with_help("Incorrect number of arguments, 2 are required.").c_str());
    }

    if (not args(0).is_string()) {
        error("%s", build_error_with_help("First argument must be a string").c_str());
    }
    if (not args(1).is_bool_scalar()) {
        error("%s", build_error_with_help("Second argument must be a bool").c_str());
    }

    auto path             = args(0).string_value();
    auto as_dynamic_array = args(1).bool_value();

    if (not fs::exists(path)) {
        error("File '%s' does not exist", path.c_str());
    } else if (not fs::is_regular_file(path)) {
        error("File '%s' is not a regular file", path.c_str());
    }

    auto file    = std::ifstream{ path };
    auto sstream = std::stringstream{};

    if (not file.is_open()) {
        error("Failed to open file '%s' (%s)", path.c_str(), strerror(errno));
    }

    sstream << file.rdbuf();

    auto json = sstream.str();            // copy, I can't move the value out from the stream
    std::stringstream{}.swap(sstream);    // to deallocate the copy

    return octave_ndjson::load(json, as_dynamic_array);
}
