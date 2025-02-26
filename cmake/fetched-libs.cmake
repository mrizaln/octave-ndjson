set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

# simdjson
# --------
FetchContent_Declare(
  simdjson
  URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.12.2.tar.gz
  URL_HASH SHA256=8ac7c97073d5079f54ad66d04381ec75e1169c2e20bfe9b6500bc81304da3faf
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(simdjson)

# fix unable to link
target_compile_options(simdjson PRIVATE -fPIC)
target_link_options(simdjson PRIVATE -fPIC)

add_library(fetch::simdjson ALIAS simdjson)
# --------

# dtl-modern
# ----------
FetchContent_Declare(
  dtl-modern
  GIT_REPOSITORY https://github.com/mrizaln/dtl-modern
  GIT_TAG v1.0.0)
FetchContent_MakeAvailable(dtl-modern)

add_library(fetch::dtl-modern ALIAS dtl-modern)
# ----------
