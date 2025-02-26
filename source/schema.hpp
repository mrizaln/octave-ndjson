#pragma once

#include "util.hpp"

#include <limits>
#include <ranges>
#include <variant>
#include <vector>

namespace octave_ndjson
{
    namespace sv = std::views;

    class Schema
    {
    public:
        template <std::ranges::range R>
        friend Schema parse_schema(R&& schema);

        // clang-format off
        enum class Scalar { Number, String, Bool, Null };
        enum class Object { Begin, End };
        enum class Array  { Begin, End };
        struct Key        { std::string m_key; bool operator==(const Key&) const = default; };
        // clang-format on

        using Part = std::variant<Scalar, Object, Array, Key>;

        static constexpr auto valid = std::numeric_limits<std::size_t>::max();

        Schema(std::size_t reserve) { m_parts.reserve(reserve); }

        void        push(Part part) { m_parts.push_back(part); }
        std::size_t size() const { return m_parts.size(); }

        bool operator==(const Schema& other) const
        {
            if (m_parts.size() != other.m_parts.size()) {
                return false;
            }
            for (auto i : sv::iota(0u, m_parts.size())) {
                if (m_parts[i] != other.m_parts[i]) {
                    return false;
                }
            }
            return true;
        }

        std::string stringify() const
        {
            enum class Kind
            {
                Object,
                Array,
            };

            auto buffer    = std::string{};
            auto traversal = std::vector<Kind>{};

            auto visit = util::Overload{
                [&](const Scalar& scalar) {
                    if (not traversal.empty() and traversal.back() == Kind::Array) {
                        buffer.append(traversal.size(), '\t');
                    }

                    switch (scalar) {
                    case Scalar::Number: buffer += "<number>,\n"; return true;
                    case Scalar::String: buffer += "<string>,\n"; return true;
                    case Scalar::Bool: buffer += "<bool>,\n"; return true;
                    case Scalar::Null: buffer += "null,\n"; return true;
                    default: [[unlikely]] std::abort();
                    }
                },
                [&](const Object& object) {
                    switch (object) {
                    case Object::Begin:
                        if (not traversal.empty() and traversal.back() == Kind::Array) {
                            buffer.append(traversal.size(), '\t');
                        }
                        buffer += "{\n";
                        traversal.push_back(Kind::Object);
                        return true;
                    case Object::End:
                        if (traversal.back() != Kind::Object) {
                            return false;
                        }
                        traversal.pop_back();
                        buffer.append(traversal.size(), '\t');
                        buffer += "},\n";
                        return true;
                    default: [[unlikely]] std::abort();
                    }
                },
                [&](const Array& array) {
                    switch (array) {
                    case Array::Begin:
                        if (not traversal.empty() and traversal.back() == Kind::Array) {
                            buffer.append(traversal.size(), '\t');
                        }
                        buffer += "[\n";
                        traversal.push_back(Kind::Array);
                        return true;
                    case Array::End:
                        if (traversal.back() != Kind::Array) {
                            return false;
                        }
                        traversal.pop_back();
                        buffer.append(traversal.size(), '\t');
                        buffer += "],\n";
                        return true;
                    default: [[unlikely]] std::abort();
                    }
                },
                [&](const Key& key) {
                    buffer.append(traversal.size(), '\t');
                    buffer.push_back('\"');
                    buffer.append(key.m_key.begin(), key.m_key.end());
                    buffer += "\": ";
                    return true;
                },
            };

            for (const auto& part : m_parts) {
                auto cont = std::visit(visit, part);
                if (not cont) {
                    return buffer += "<invalid_after_this>";
                }
            }

            return buffer;
        }

    private:
        std::vector<Part> m_parts;
    };
}
