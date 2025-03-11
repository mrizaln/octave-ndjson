#include "args.hpp"

#include "ndjson_load.hpp"

#include <octave/error.h>
#include <octave/ov.h>
#include <octave/ovl.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>

namespace octave_ndjson::args::detail
{
    std::optional<ParseMode> parse_mode_from_string(std::string_view str)
    {
        // clang-format off
        if      (str == "strict")   return ParseMode::Strict;
        else if (str == "dynarray") return ParseMode::DynamicArray;
        else if (str == "relaxed")  return ParseMode::Relaxed;
        else                        return std::nullopt;
        // clang-format on
    }

    std::optional<Threading> threading_from_string(std::string_view str)
    {
        // clang-format off
        if      (str == "single") return Threading::Single;
        else if (str == "multi")  return Threading::Multi;
        else                      return std::nullopt;
        // clang-format on
    }
}

namespace octave_ndjson::args
{
    ParsedArgs parse(const octave_value_list& args, Kind kind, const char* error_prefix)
    {
        const auto prefixed_error = [&](const char* message) { error("%s\n%s", error_prefix, message); };

        const auto args_str = [&](long i, bool to_lower, const char* error_message) {
            if (not args(i).is_string()) {
                prefixed_error(error_message);
            }

            auto value = args(i).string_value();
            if (to_lower) {
                auto lower = [](char c) { return static_cast<char>(std::tolower(c)); };
                std::transform(value.begin(), value.end(), value.begin(), lower);
            }
            return value;
        };

        const auto size = args.length();
        if (size < 1) {
            prefixed_error("Incorrect number of arguments, at least 1 is required.");
        }

        auto parsed = ParsedArgs{
            .m_path_or_string = "",
            .m_mode           = ParseMode::Strict,
            .m_threading      = Threading::Multi,
        };

        switch (kind) {
        case Kind::File:
            parsed.m_path_or_string = args_str(0, false, "First argument must be a file path");
            break;
        case Kind::String:
            parsed.m_path_or_string = args_str(0, false, "First argument must be a string");
            break;
        }

        auto i = 1l;
        while (i < size) {
            auto param = args_str(i++, true, "Expected a string parameter");

            if (i >= size) {
                prefixed_error(std::format("Expected a value for parameter '{}'", param).c_str());
            }

            if (param == "mode") {
                auto value = args_str(i++, true, "Expected a string value for 'mode'");
                auto mode  = detail::parse_mode_from_string(value);

                if (not mode) {
                    prefixed_error(std::format("Invalid value '{}' for 'mode'", value).c_str());
                }
                parsed.m_mode = *mode;
            } else if (param == "threading") {
                auto value = args_str(i++, true, "Expected a string value for 'threading'");
                auto mode  = detail::threading_from_string(value);

                if (not mode) {
                    prefixed_error(std::format("Invalid value '{}' for 'threading'", value).c_str());
                }
                parsed.m_threading = *mode;
            } else {
                prefixed_error(std::format("Unknown parameter '{}'", param).c_str());
            }
        }

        return parsed;
    }
}
