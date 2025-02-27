#include "ndjson_load.hpp"

static constexpr auto usage_string = R"(
ndjson_load_string(jsonl_string: string, dynamic_array: bool, single_threaded: bool)
)";

static constexpr auto help_string = R"(
============================== ndjson_load_string help page ==============================
signature:
    ndjson_load_string(jsonl_string: string, dynamic_array: bool, single_threaded: bool)

parameters:
    > jsonl_string      : An NDJSON/JSON Lines string.
    > dynamic_array     : This bool flag signals the parser to parse (and validate) arrays
                          as dynamicallly sized array. This flag also turn off the type
                          check on the arrays elements.
    > single_threaded   : Run in single-thread mode instead. Multithread mode will be much
                          faster, but it uses a lot of memory as well, you may want to set
                          this flag to true if you are constrained in memory.

behavior:
    By default the [ndjson_load_string] function will parse NDJSON/JSON Lines ([jsonl]
    from hereon) in strict mode i.e. all the documents on the [jsonl] must have the same
    JSON structure from the number of elements of an array, the type of each element, type
    type of object value, to the order of the occurence of the key in the document.

    The [ndjson_load_string] function will run in multithreaded mode by default. The only
    caveat is that you must have each JSON document at each line (don't prettify). So, the
    input must be like this

        > ok                                    > only the first array processed
    ```                                     ```
    [1, 2, 3, 4]                            [1, 2, 3, 4] [5, 6, 7, 8] [9, 10, 11, 12]
    [5, 6, 7, 8]                            ```
    [9, 10, 11, 12]
    ```

    The single-thread mode don't have this constraint.

example:
    For example, a [jsonl] string with content:
        "[1, 2, 3, 4]\n[5, 6, 7]"

    if parsed will return an error with message:

    ```
        octave> ndjson_load_string("[1, 2, 3, 4]\n[5, 6, 7]", false, false)

        error: Parsing error
            > Mismatched schema, all documents must have same schema (dynamic_array flag not enabled)

        First document:
        [
            <number> x 4,
        ],

        Current document (line: 2):
        [
            <number> x 3,
        ],


            > around: [<bol>[5, 6, 7]<eol>] (line: 2 | offset: 0)
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

DEFUN_DLD(ndjson_load_string, args, , usage_string)
{
    if (args.length() != 3) {
        error("%s", build_error_with_help("Incorrect number of arguments, 3 are required.").c_str());
    }

    if (not args(0).is_string()) {
        error("%s", build_error_with_help("First argument must be a string").c_str());
    }
    if (not args(1).is_bool_scalar()) {
        error("%s", build_error_with_help("Second argument must be a bool").c_str());
    }
    if (not args(2).is_bool_scalar()) {
        error("%s", build_error_with_help("Third argument must be a bool").c_str());
    }

    auto json             = args(0).string_value();
    auto as_dynamic_array = args(1).bool_value();
    auto single_threaded  = args(2).bool_value();

    if (single_threaded) {
        return octave_ndjson::load(json, as_dynamic_array);
    } else {
        return octave_ndjson::load_multi(json, as_dynamic_array);
    }
}
