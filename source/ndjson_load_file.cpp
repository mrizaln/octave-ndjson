#include "ndjson_load.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

DEFUN_DLD(ndjson_load_file, args, , "ndjson_load_file(<filepath>)")
{
    if (args.length() != 1) {
        octave::print_usage();
    }

    auto path = args(0).string_value();
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

    return octave_ndjson::load(json);
}
