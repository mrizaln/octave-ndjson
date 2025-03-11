cmake_minimum_required(VERSION 3.16)
project(octave-ndjson VERSION 0.0.1)

include(cmake/prelude.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(octave REQUIRED IMPORTED_TARGET octave)

include(cmake/fetched-libs.cmake) # emits fetch::*

# ndjson_load general
# -------------------
add_library(
    ndjson_load
    STATIC
    source/ndjson_load.cpp
    source/args.cpp
    source/parse_octave_value.cpp
    source/schema.cpp
)
target_include_directories(ndjson_load SYSTEM PUBLIC ${octave_INCLUDE_DIRS})
target_link_libraries(ndjson_load PUBLIC fetch::simdjson fetch::dtl-modern)
target_compile_options(ndjson_load PRIVATE -Wall -Wextra -Wconversion)

# fix unable to link
target_compile_options(ndjson_load PRIVATE -fPIC)
target_link_options(ndjson_load PRIVATE -fPIC)
# -------------------

function(make_oct name)
    add_library(${name} SHARED source/${name}.cpp)
    target_link_libraries(${name} PRIVATE ndjson_load)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wconversion)
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".oct")
endfunction()

make_oct(ndjson_load_string)
make_oct(ndjson_load_file)
