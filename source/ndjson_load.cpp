#include "ndjson_load.hpp"

#include "parse_octave_value.hpp"
#include "schema.hpp"
#include "util.hpp"

#include <octave/error.h>
#include <octave/ov.h>
#include <simdjson.h>

#include <format>
#include <string>
#include <vector>

namespace octave_ndjson::detail
{
    /**
     * @brief Create a string with escaped whitespace.
     *
     * @param string The string to be escaped.
     *
     * @return An escaped string.
     */
    std::string escape_whitespace(std::string_view string) noexcept
    {
        auto escaped = std::string{};
        for (auto ch : string) {
            switch (ch) {
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            case '\r': escaped += "\\r"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\v': escaped += "\\v"; break;
            default: escaped += ch; break;
            }
        }
        return escaped;
    }

    /**
     * @brief Build a schema from simdjson dom element.
     *
     * @param schema The schema out parameter.
     * @param elem The simdjson dom element.
     *
     * I specifically use out parameter here so I can reuse the Schema, thus reducing memory usage.
     */
    void build_schema(Schema& schema, simdjson::dom::element elem)
    {
        using T = simdjson::dom::element_type;
        switch (elem.type()) {
        case T::ARRAY: {
            schema.push(Schema::Array::Begin);
            for (auto v : simdjson::dom::array{ elem }) {
                build_schema(schema, v);
            }
            schema.push(Schema::Array::End);
        } break;
        case T::OBJECT: {
            schema.push(Schema::Object::Begin);
            for (auto [k, v] : simdjson::dom::object{ elem }) {
                schema.push(Schema::Key{ { k.data(), k.size() } });
                build_schema(schema, v);
            }
            schema.push(Schema::Object::End);
        } break;
        case T::INT64: schema.push(Schema::Scalar::Number); break;
        case T::UINT64: schema.push(Schema::Scalar::Number); break;
        case T::DOUBLE: schema.push(Schema::Scalar::Number); break;
        case T::STRING: schema.push(Schema::Scalar::String); break;
        case T::BOOL: schema.push(Schema::Scalar::Bool); break;
        case T::NULL_VALUE: schema.push(Schema::Scalar::Null); break;
        }
    }

}

namespace octave_ndjson
{
    namespace sv = std::views;

    octave_value load(simdjson::padded_string_view string, ParseMode mode)
    {
        auto parser       = simdjson::dom::parser{};
        auto maybe_stream = parser.parse_many(
            string.data(), string.size(), simdjson::dom::DEFAULT_BATCH_SIZE
        );

        if (maybe_stream.error()) {
            error("failed to initilize simdjson: %s", simdjson::error_message(maybe_stream.error()));
        }

        auto stream           = std::move(maybe_stream).take_value();
        auto docs             = std::vector<octave_value>{};
        auto reference_schema = std::optional<Schema>{};
        auto schema           = Schema{ 0 };

        for (auto it = stream.begin(); it != stream.end(); ++it) {
            auto dom = *it;
            try {
                auto parsed = parse_octave_value(dom.value());

                if (mode == ParseMode::Relaxed) {
                    docs.push_back(std::move(parsed));
                    continue;
                }

                schema.reset();
                detail::build_schema(schema, dom.value());

                if (not reference_schema.has_value()) {
                    reference_schema = schema;
                }

                if (not reference_schema->is_same(schema, mode == ParseMode::DynamicArray)) {
                    auto [reference_diff, current_diff] = util::create_diff(
                        reference_schema->stringify(mode == ParseMode::DynamicArray),
                        schema.stringify(mode == ParseMode::DynamicArray)
                    );
                    auto message = std::format(
                        "Mismatched schema, all documents must have the same schema"
                        "\n\nFirst document:\n{0:}\nCurrent document (document number: {2:}):\n{1:}",
                        reference_diff,
                        current_diff,
                        docs.size()
                    );

                    throw std::runtime_error{ message };
                }

                docs.push_back(std::move(parsed));
            } catch (std::exception& e) {
                auto offset = it.current_index();
                auto to_end = string.size() - static_cast<std::size_t>(offset);
                auto substr = detail::escape_whitespace({ string.data() + offset, std::min(to_end, 50ul) });

                auto message = std::format(
                    "Parsing error\n"
                    "\t> {}\n\n"
                    "\t> around: [{}\033[1;33m{}\033[00m{}] (at offset: {})\n"
                    "\t                ^\n"
                    "\t                |\n"
                    "\t  parsing ends here",
                    e.what(),
                    offset > 0 ? " ... " : "<bof>",
                    substr,
                    to_end > 50ul ? " ... " : "<eof>",
                    offset
                );

                error("%s", message.c_str());
            }
        }

        if (docs.size() == 1) {
            if (reference_schema->root_is_object()) {
                return docs[0].scalar_map_value();
            } else if (docs[0].isnumeric()) {
                return docs[0].array_value();
            } else {
                return docs[0].cell_value();
            }
        }

        if (mode != ParseMode::Relaxed and reference_schema->root_is_object()) {
            auto struct_array      = octave_map{};
            auto struct_array_dims = dim_vector{ static_cast<long>(docs.size()), 1 };
            auto field_names       = docs[0].scalar_map_value().fieldnames();

            if (field_names.numel() != 0) {
                auto value = Cell{ struct_array_dims };
                for (auto i : sv::iota(0l, field_names.numel())) {
                    for (auto k : sv::iota(0u, docs.size())) {
                        value(k) = docs[k].scalar_map_value().getfield(field_names(i));
                    }
                    struct_array.assign(field_names(i), value);
                }
                return struct_array;
            }
        }

        auto cell = Array<octave_value>{ dim_vector{ 1, static_cast<long>(docs.size()) } };
        for (auto i = 0ul; i < docs.size(); ++i) {
            cell(0, static_cast<long>(i)) = docs[i];
        }
        return cell;
    }

    octave_value load_multi(simdjson::padded_string_view string, ParseMode mode)
    {
        auto lines = util::split(string, '\n');

        if (lines.size() == 0) {
            return NDArray{};
        } else if (lines.size() == 1) {
            return load(string, mode);
        }

        static constexpr auto no_exception = std::numeric_limits<std::size_t>::max();

        auto first_line = lines[0];
        auto rem_lines  = std::span{ lines.begin() + 1, lines.size() - 1 };

        // NOTE: too high number of concurrency leads to slower parsing time. I cannot know for sure what
        // causes this, but I highly suspect that this caused by memory contention of some sort. This
        // bottleneck issue is not a trivial one and from my testing, halving the number of threads works
        // best, though letting the user control the number of threads at runtime may be better.

        auto concurrency = std::max(std::thread::hardware_concurrency() / 2, 1u);
        concurrency      = std::min(concurrency, static_cast<unsigned int>(rem_lines.size()));

        auto block_size = rem_lines.size() / concurrency;
        auto parsers    = std::vector<simdjson::dom::parser>(concurrency);
        auto threads    = std::vector<std::jthread>{};

        auto cell            = Cell{ dim_vector(static_cast<long>(lines.size()), 1) };
        auto exception       = std::exception_ptr{};
        auto exception_index = std::atomic<std::size_t>{ no_exception };

        auto reference_schema = Schema{ 0 };

        auto parse_fn = [&](simdjson::dom::parser& parser, std::span<std::string_view> block, long offset) {
            auto schema = Schema{ 0 };

            for (auto i = 0u; auto line : block) {
                if (exception_index != std::numeric_limits<std::size_t>::max()) {
                    break;
                }

                try {
                    auto dom             = parser.parse(line.data(), line.size(), false).value();
                    cell(offset + i + 1) = parse_octave_value(dom);    // 1st line is skipped

                    if (mode != ParseMode::Relaxed) {
                        schema.reset();
                        detail::build_schema(schema, dom);

                        if (not reference_schema.is_same(schema, mode == ParseMode::DynamicArray)) {
                            auto [reference_diff, current_diff] = util::create_diff(
                                reference_schema.stringify(mode == ParseMode::DynamicArray),
                                schema.stringify(mode == ParseMode::DynamicArray)
                            );
                            throw std::runtime_error{ std::format(
                                "Mismatched schema, all documents must have the same schema"
                                "\n\nFirst document:\n{0:}\nCurrent document (document number: {2:}):\n{1:}",
                                reference_diff,
                                current_diff,
                                offset + i + 2    // line numbering is 1-indexed; 1st line is skipped
                            ) };
                        }
                    }
                } catch (...) {
                    auto idx = static_cast<std::size_t>(offset) + i + 1;    // 1st line is skipped
                    if (auto i = no_exception; exception_index.compare_exchange_strong(i, idx)) {
                        exception = std::current_exception();
                    }
                }
                ++i;
            }
        };

        try {
            // the first line is parsed separately here to get the reference schema
            auto dom = parsers[0].parse(first_line.data(), first_line.size(), false);
            if (dom.error()) {
                exception_index = 0;
                throw simdjson::simdjson_error{ dom.error() };
            } else {
                cell(0) = parse_octave_value(dom.value());
                if (mode != ParseMode::Relaxed) {
                    detail::build_schema(reference_schema, dom.value());
                };
            }

            // the rest is parsed here
            for (auto i : sv::iota(0u, concurrency)) {
                auto offset       = i * block_size;    // first line is already parsed
                auto current_size = std::min(block_size, rem_lines.size() - offset);
                auto block        = std::span{ rem_lines.begin() + static_cast<long>(offset), current_size };
                threads.emplace_back(parse_fn, std::ref(parsers[i]), block, offset);
            }
            for (auto& thread : threads) {
                thread.join();
            }
            if (exception_index != no_exception) {
                std::rethrow_exception(exception);
            }
        } catch (std::exception& e) {
            auto line   = lines[exception_index];
            auto substr = detail::escape_whitespace(line.substr(0, std::min(line.size(), 50ul)));

            auto message = std::format(
                "Parsing error\n"
                "\t> {}\n\n"
                "\t> around: [<bol>\033[1;33m{}\033[00m{}] (line: {})\n"
                "\t                ^\n"
                "\t                |\n"
                "\t  parsing ends here",
                e.what(),
                substr,
                line.size() > 50ul ? " ... " : "<eol>",
                exception_index + 1    // line numbering is 1-indexed
            );

            error("%s", message.c_str());
        }

        if (cell.numel() == 1) {
            if (reference_schema.root_is_object()) {
                return cell(0).scalar_map_value();
            } else if (cell(0).isnumeric()) {
                return cell(0).array_value();
            } else {
                return cell(0).cell_value();
            }
        }

        if (mode != ParseMode::Relaxed and reference_schema.root_is_object()) {
            auto struct_array      = octave_map{};
            auto struct_array_dims = dim_vector{ cell.numel(), 1 };
            auto field_names       = cell(0).scalar_map_value().fieldnames();

            if (field_names.numel() != 0) {
                auto value = Cell{ struct_array_dims };
                for (auto i : sv::iota(0l, field_names.numel())) {
                    for (auto k : sv::iota(0l, cell.numel())) {
                        value(k) = cell(k).scalar_map_value().getfield(field_names(i));
                    }
                    struct_array.assign(field_names(i), value);
                }
                return struct_array;
            }
        }

        return cell;
    }
}
