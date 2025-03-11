# octave-ndjson

Multithreaded Newline Delimited JSON (`.ndjson`) or JSON Lines (`.jsonl`) parser for Octave, powered by [`simdjson`](https://github.com/simdjson/simdjson).

This library is inspired by [`octave-rapidjson`](https://github.com/Andy1978/octave-rapidjson) but the design follows [`jsondecode`](https://docs.octave.org/latest/JSON-data-encoding_002fdecoding.html#index-jsondecode) function.

## Motivation

I have a simulation that emits data every few seconds. The data is encoded in [Newline Delimited JSON](https://github.com/ndjson/ndjson-spec)/[JSON Lines](https://jsonlines.org/).

Before I created this, I need to always convert my JSONL into a JSON using `jq`. For a while it was okay, but the more the JSONL contains data, the longer the conversion and the higher the memory consumption is. Until after my JSONL file contains about `100k` JSON document entries in it and about `200MiB` in size, `jq` consume so much memory that the OOM killer got triggered and killed it.

After looking for a while on the internet I found that there is no parser for such JSON format for Octave, so I decided to create my own.

## Usage

> For more comprehensive usage information, read the [Help](#help) section.

You can imagine JSONL as a JSON with an array as its root :

> JSON

```json
[
  { "a": 1, "b": 2 },
  { "a": 3, "b": 4 }
]
```

> JSONL

```json
{ "a": 1, "b": 2 }
{ "a": 3, "b": 4 }
```

This format is used primarily in streaming data (like my simulation).

While yes there is `jq` tool that can convert the data from `json` to `jsonl` (and vice versa) it adds the overhead of converting them which takes quite a while if the data is big (and may use too much resource, as illustrated above). Using this format is also just easier to handle since we don't need to add and remove the square brackets every time data is transferred to be appended to already received data.

### Parsing JSONL

There are two functions that you can use to parse a JSONL document.

- `ndjson_load_string`, which takes a JSON string as parameter;

  ```
  octave:2> x = ndjson_load_string("{ \"a\": 1, \"b\": 3.14 }\n{ \"a\": 2, \"b\": 2.71 }")
  x =

    2x1 struct array containing the fields:

      a
      b

  octave:3> x(1)
  ans =

    scalar structure containing the fields:

      a = 1
      b = 3.1400

  octave:4> x(2)
  ans =

    scalar structure containing the fields:

      a = 2
      b = 2.7100

  octave:5>

  ```

- and `ndjson_load_file`, which takes a filepath (string) as its parameter.

  Given a `data.jsonl` file in the Octave working directory with following content

  ```json
  { "a": 1, "b": 3.14 }
  { "a": 2, "b": 2.71 }
  ```

  you can load it with the function like so:

  ```
  octave:5> x = ndjson_load_file('data.jsonl')
  x =

    2x1 struct array containing the fields:

      a
      b

  octave:6> x(1)
  ans =

    scalar structure containing the fields:

      a = 1
      b = 3.1400

  octave:7> x(2)
  ans =

    scalar structure containing the fields:

      a = 2
      b = 2.7100

  ```

### Parsing JSON

Since A JSON file is just a JSONL file with single document, you can also use this library to parse them.

> A JSON with the name `data.json`

```json
{ "a": 1, "b": 3.14 }
```

```
octave:3> ndjson_load_file('data.jsonl')
ans =

  scalar structure containing the fields:

    a = 1
    b = 3.1400

```

## Building

This is a C++ code so you need to compile the code first before using it.

### Build dependencies

- C++20 capable compiler
- CMake 3.16+

### Library dependencies

- Octave header
- simdjson

The simdjson library is fetched directly using CMake so no need to prepare for that one but for the Octave headers you need to install them first on your system:

- Fedora

  `sudo dnf install octave-devel`

- Ubuntu

  `sudo apt install octave-dev`

For other distros, you might want to search for the information in your distro's repository and adjust accordingly.

> For Windows... idk

### Compiling

The compile step is quite simple, you just need to run these command one after another:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release      # you can use -G Ninja if you want to use Ninja instead of Make
cmake --build build
```

The resulting `.oct` files will be in the `./build` directory relative to the project root. You can move them to your Octave working directory and use it!

### Generate code documentation

> - The documentation is only needed if you want to understand how the code works. Do use the it if you want to contribute back to the repo :D
> - The codebase is short enough, so you should be able to understand everything by looking at the comment in source code directly, so this step might be not necessary for you

To generate the documentation, you need to have `doxygen` installed on your system.

```sh
doxygen docs/Doxygen
```

The documentation will be generated inside `docs/doxygen/html` directory. Use your favorite browser (or html viewer) to view it.

```sh
firefox docs/doxygen/html/index.html
```

## Note

- The multithreaded parser is sensitive to newlines. Please reserve newline for separating documents only (like a good NDJSON/JSONL file).
- If you have a prettified JSON, you probably want to unprettify them first if you want to use the multithreading capability, but if you don't want to, you can always fallback to the single-thread mode by setting parameter `'threading'` to `'single'`.

  > A JSON with the name `data.json`

  ```json
  {
    "a": 1,
    "b": 3.14
  }
  ```

  ```
  octave:12> a = ndjson_load_file('data.jsonl', 'threading', 'single')
  a =

    scalar structure containing the fields:

      a = 1
      b = 3.1400
  ```

## Benchmark

The benchmark is done on an Intel(R) Core(TM) i5-10500H (6 core/12 thread) with the frequency locked to 2.5GHz. The file used to benchmark the functions is a JSON/JSONL file with `199034` document entries (array elements if JSON). Each document is `1969.17 ± 269.162` bytes long, amounts to a file `374 MiB` big.

| function             | note                   |        time | speedup |
| :------------------- | :--------------------- | ----------: | ------: |
| `jsondecode`         | native octave function | `16.35900s` | `1.00x` |
| `ndjson_load_string` | single thread, relaxed |  `9.17047s` | `1.78x` |
| `ndjson_load_string` | multi thread, relaxed  |  `3.22453s` | `5.07x` |
| `ndjson_load_file`   | single thread, relaxed |  `8.01613s` | `2.04x` |
| `ndjson_load_file`   | multi thread, relaxed  |  `2.54787s` | `6.42x` |
| `load_json`          | octave-rapidjson       | `27.85150s` | `0.59x` |

> - I'm actually quite disappointed with the result. The speedup from single thread to multi thread is only `3.15x`. This is not a very good value, considering the number of cores and threads my test computer has. But, the increase is not marginal either, so it's still a win.
> - `simdjson`'s dom parser on `ndjson` is multithreaded by default (2 threads: main thread and worker thread--It is detailed [here](https://github.com/simdjson/simdjson/blob/f3b034ac38060303c856c83f51f4156a4d1da8c1/doc/parse_many.md#threads)). So even when `ndjson_load_string` or `ndjson_load_file` ran in single thread mode, it may spawn two threads (it can be disabled when compiling, refer to `simdjson` documentation).

## Help

This is the full usage information of the two functions

> `ndjson_load_string`

````
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

    if parsed with default parameters will return an error with message:

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
````

> `ndjson_load_file`

````
=============================== ndjson_load_file help page ===============================
signature:
    ndjson_load_file(
        filepath  : string,         % positional
        [mode     : enum_string],   % optional property
        [threading: enum_string]    % optional property
    )

parameters:
    > filepath : Must be a string that points to a file.

    > mode : Enumeration that specifies the strictness of the schema comparison.
        - strict   : Documents must have the same schema.
        - dynarray : Documents have the same schema but the number of elements in array
                     and its types can vary.
        - relaxed  : Documents can have different schemas.

    > threading : Threading mode.
        - single : Run in single-thread mode.
        - multi  : Run in multi-thread mode.

behavior:
    By default the [ndjson_load_file] function will parse NDJSON/JSON Lines ([jsonl] from
    hereon) in strict mode i.e. all the documents on the [jsonl] must have the same JSON
    structure (the number of elements of an array, the type of each element, type type
    of object value, and the order of the occurence of the key in the document).

    The [ndjson_load_file] function will run in multithreaded mode by default. The only
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
    For example, a [data.jsonl] file with content:
    ```
        { "a": 1, "b": [4, 5] }
        { "a": 2, "b": [6, 7, 8] }
    ```

    if parsed with default parameters will return an error with message:

    ```
        octave> ndjson_load_file('data.jsonl')

        error: Parsing error
            > Mismatched schema, all documents must have same schema (dynamic_array: false)

        % rest of the message...
    ```

    You can relax the schema comparison by setting the `mode` parameter to 'dynarray'
    (or 'relaxed' if you want to ignore the schema comparison entirely):

    ```
        octave> a = ndjson_load_file('data.jsonl', 'mode', 'dynarray');
        octave> % success!
    ```
==========================================================================================
````

## TODO

- [ ] ~~Eliminate the constraint of each JSON document needed to be separated by newline.~~
  > I essentially need to create a simpler JSON parser for this, not worth it (I've tried).
- [x] Optimize ~~`parse_json_value`~~ `parse_octave_value` function.
  > Using dom parser is better apparently. Also, I kinda copied `jsondecode` source code, so that's that.
- [ ] Add on-demand file read approach.
  > Line-by-line buffering mechanism is the best approach I guess.
- [ ] Add the ability to set number of threads at runtime.
