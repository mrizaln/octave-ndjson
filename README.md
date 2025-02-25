# octave-ndjson

Newline Delimited JSON (ndjson) or JSON Lines (jsonl) parser for Octave.

[simdjson](https://github.com/simdjson/simdjson) is used as the underlying JSON parsing library.

This library is inspired largely by [octave-rapidjson](https://github.com/Andy1978/octave-rapidjson) design instead of the built-in `jsondecode` function in Octave.

## Motivation

I have a simulation that emits data every few seconds. The data is encoded in [Newline Delimited JSON](https://github.com/ndjson/ndjson-spec)/[JSON Lines](https://jsonlines.org/). After looking for a while on the internet I found that there is no parser for such JSON format for Octave. So here it is.

You can image JSONL as a JSON with the Root object an Array:

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

While yes there is `jq` tool that can convert the data from `json` to `jsonl` (and vice versa) it adds the overhead of converting them which takes quite a while if the data is big (hundreds of megabytes). Using this format also is just easier to handle since we don't need to add and remove the square brackets every time data is transferred to be appended to already received data.

## Usage

There are two functions that you can use to parse a JSON document.

- `ndjson_load_string`, which takes a JSON string as parameter;

  ```
  octave> x = ndjson_load_string('{ "a": 1, "b": 3.14 } { "a": 2, "b": 2.71 }')

  x =
  {
    [1,1] =

      scalar structure containing the fields:

        a = 1
        b = 3.14

    [1,2] =

      scalar structure containing the fields:

        a = 2
        b = 2.71

  }
  ```

- and `ndjson_load_file`, which takes a string as filepath as its parameter.

  Given a `data.jsonl` file in the Octave working directory with following content

  ```json
  { "a": 1, "b": 3.14 }
  { "a": 2, "b": 2.71 }
  ```

  you can load it with the function like so:

  ```
  octave> x = ndjson_load_string('data.jsonl')

  x =
  {
    [1,1] =

      scalar structure containing the fields:

        a = 1
        b = 3.14

    [1,2] =

      scalar structure containing the fields:

        a = 2
        b = 2.71

  }
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

### Compiling

The compile step is quite simple, you just need to run these command one after another:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release      # you can use -G Ninja if you want to use Ninja instead of Make
cmake --build build
```

The resulting `.oct` files will be in the `./build` directory relative to the project root. You can move them to your Octave working directory and use it!
