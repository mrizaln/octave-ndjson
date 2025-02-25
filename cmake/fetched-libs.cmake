set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

FetchContent_Declare(
  simdjson
  URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.12.2.tar.gz
  URL_HASH SHA256=8ac7c97073d5079f54ad66d04381ec75e1169c2e20bfe9b6500bc81304da3faf
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(simdjson)

target_compile_options(simdjson PRIVATE -fPIC)
target_link_options(simdjson PRIVATE -fPIC)

add_library(fetch::simdjson ALIAS simdjson)
