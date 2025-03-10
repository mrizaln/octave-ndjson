#include "args.hpp"
#include "ndjson_load.hpp"

static constexpr auto usage_string = R"(
ndjson_load_string(
    json_string: string,         % positional
    [mode      : enum_string],   % optional property
    [threading : enum_string]    % optional property
)
)";

static constexpr auto help_string = R"(
============================== ndjson_load_string help page ==============================
signature:
    ndjson_load_string(
        json_string: string,         % positional
        [mode      : enum_string],   % optional property
        [threading : enum_string]    % optional property
    )

parameters:
    > json_string : An NDJSON/JSON Lines string.

    > mode : Enumeration that specifies the strictness of the schema comparison.
        - strict   : Documents must have the same schema.
        - dynarray : Documents have the same schema but the number of elements in array
                     and its types can vary.
        - relaxed  : Documents can have different schemas.

    > threading : Threading mode.
        - single : Run in single-thread mode.
        - multi  : Run in multi-thread mode.

behavior:
    By default the [ndjson_load_string] function will parse NDJSON/JSON Lines ([jsonl] from
    hereon) in strict mode i.e. all the documents on the [jsonl] must have the same JSON
    structure (the number of elements of an array, the type of each element, type type
    of object value, and the order of the occurence of the key in the document).

    The [ndjson_load_string] function will run in multithreaded mode by default. The only
    caveat is that you must have each JSON document at each line (don't prettify). So, the
    input must be like this:

    ```
        { "a": 1, "b": [4, 5] }
        { "a": 2, "b": [6, 7] }
    ```

    This one will result in an error:

    ```
        {                           // <- parsing ends here: incomplete object
            "a": 1,
            "b": [4, 5]
        }
        {
            "a": 2,
            "b": [6, 7]
        }
    ```

    The single-thread mode don't have this constraint.

example:
    For example, a variable [data] which is a string with content:
    ```
        { "a": 1, "b": [4, 5] }
        { "a": 2, "b": [6, 7, 8] }
    ```

    if parsed will default parameters will return an error with message:

    ```
        octave> ndjson_load_string(data)

        error: Parsing error
            > Mismatched schema, all documents must have same schema (dynamic_array: false)

        % rest of the message...
    ```

    You can relax the schema comparison by setting the `mode` parameter to 'dynarray'
    (or 'relaxed' if you want to ignore the schema comparison entirely):

    ```
        octave> a = ndjson_load_string(data, 'mode', 'dynarray');
        octave> % success!
    ```
==========================================================================================
)";

namespace ndjson = octave_ndjson;

DEFUN_DLD(ndjson_load_string, args, , usage_string)
{
    auto [json, mode, threading] = ndjson::args::parse(args, ndjson::args::Kind::String, help_string);

    switch (threading) {
    case ndjson::args::Threading::Single: return ndjson::load(json, mode);
    case ndjson::args::Threading::Multi: return ndjson::load_multi(json, mode);
    default: std::abort();
    }
}
