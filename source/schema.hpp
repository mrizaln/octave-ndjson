#pragma once

#include "util.hpp"

#include <octave/error.h>

#include <ranges>
#include <variant>
#include <vector>

namespace octave_ndjson
{
    namespace sv = std::views;
    namespace sr = std::ranges;

    /**
     * @class Schema
     *
     * @brief A simple representation of JSON schema.
     */
    class Schema
    {
    public:
        // clang-format off
        enum class Scalar { Number, String, Bool, Null };
        enum class Object { Begin, End };
        enum class Array  { Begin, End };
        struct Key        { std::string m_key; bool operator==(const Key&) const = default; };
        // clang-format on

        using Part = std::variant<Scalar, Object, Array, Key>;

        Schema(std::size_t reserve) noexcept { m_parts.reserve(reserve); }

        void        push(Part part) noexcept { m_parts.push_back(part); }
        std::size_t size() const noexcept { return m_parts.size(); }
        void        reset() noexcept { m_parts.clear(); }

        /**
         * @brief Check if the root of the document is an object
         */
        bool root_is_object() const noexcept
        {
            return m_parts.empty() ? false : std::holds_alternative<Object>(m_parts.front());
        }

        /**
         * @brief Check whether the schema is the same as the other schema.
         *
         * @param other The other schema to be compared with.
         * @param dynamic_array Dynamic array mode flag.
         *
         * @return True if the schema is the same, false otherwise.
         *
         * If `dynamic_array` is true, then the schema is considered the same if the structure is the same
         * regardless of the array elements. This means that the depth of the array also won't be checked and
         * if any of the elements happen to be an array or object, it won't be checked further.
         *
         * The strictest comparison is done when `dynamic_array` is false--all elements must be the same and
         * in the same order.
         */
        bool is_same(const Schema& other, bool dynamic_array) const noexcept
        {
            if (not dynamic_array) {
                if (m_parts.size() != other.m_parts.size()) {
                    return false;
                }

                for (auto i : sv::iota(0u, m_parts.size())) {
                    if (m_parts[i] != other.m_parts[i]) {
                        return false;
                    }
                }

                return true;
            } else {
                auto i = m_parts.begin();
                auto j = other.m_parts.begin();

                while (i != m_parts.end() and j != other.m_parts.end()) {
                    if (*i != *j) {
                        return false;
                    }

                    if (*i == Part{ Array::Begin }) {
                        auto obj_count = 0;
                        auto arr_count = 1;

                        do {
                            ++i;
                            if (auto* arr = std::get_if<Array>(&*i)) {
                                switch (*arr) {
                                case Array::Begin: ++arr_count; break;
                                case Array::End: --arr_count; break;
                                }
                            } else if (auto* obj = std::get_if<Object>(&*i)) {
                                switch (*obj) {
                                case Object::Begin: ++obj_count; break;
                                case Object::End: --obj_count; break;
                                }
                            }
                        } while ((obj_count > 0 or arr_count > 0) and i != m_parts.end());
                    }

                    if (*j == Part{ Array::Begin }) {
                        auto obj_count = 0;
                        auto arr_count = 1;

                        do {
                            ++j;
                            if (auto* arr = std::get_if<Array>(&*j)) {
                                switch (*arr) {
                                case Array::Begin: ++arr_count; break;
                                case Array::End: --arr_count; break;
                                }
                            } else if (auto* obj = std::get_if<Object>(&*j)) {
                                switch (*obj) {
                                case Object::Begin: ++obj_count; break;
                                case Object::End: --obj_count; break;
                                }
                            }
                        } while ((obj_count > 0 or arr_count > 0) and j != other.m_parts.end());
                    }

                    ++i;
                    ++j;
                }

                if (i != m_parts.end() or j != other.m_parts.end()) {
                    return false;
                }

                return true;
            }
        }

        /**
         * @brief Stringify the schema.
         *
         * @return The stringified schema.
         *
         * Useful for debugging.
         */
        std::string stringify(bool dynamic_array) const noexcept
        {
            enum class Kind
            {
                Object,
                Array,
            };

            auto buffer     = std::string{};
            auto traversal  = std::vector<Kind>{};
            auto prev       = Part{ Scalar::Null };
            auto prev_count = 0ul;

            auto visit = util::Overload{
                [&](const Scalar& scalar) {
                    if (not traversal.empty() and traversal.back() == Kind::Array) {
                        buffer.append(traversal.size(), '\t');
                    }

                    switch (scalar) {
                    case Scalar::Number: buffer += "<number>,\n"; return true;
                    case Scalar::String: buffer += "<string>,\n"; return true;
                    case Scalar::Bool: buffer += "<bool>,\n"; return true;
                    case Scalar::Null: buffer += "<null>,\n"; return true;
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

            auto it = m_parts.begin();
            while (it != m_parts.end()) {
                auto part = *it;
                ++it;

                if (dynamic_array and std::holds_alternative<Array>(part)) {
                    auto obj_count = 0;
                    auto arr_count = 1;

                    while ((obj_count > 0 or arr_count > 0) and it != m_parts.end()) {
                        if (auto* arr = std::get_if<Array>(&*it)) {
                            switch (*arr) {
                            case Array::Begin: ++arr_count; break;
                            case Array::End: --arr_count; break;
                            }
                        } else if (auto* obj = std::get_if<Object>(&*it)) {
                            switch (*obj) {
                            case Object::Begin: ++obj_count; break;
                            case Object::End: --obj_count; break;
                            }
                        }
                        ++it;
                    }

                    buffer += "[ <any> x N ],\n";

                    if (it == m_parts.end()) {
                        break;
                    }
                    continue;
                }

                if (not traversal.empty() and traversal.back() == Kind::Array) {
                    if (prev_count == 0 and std::holds_alternative<Scalar>(part)) {
                        prev = part;
                        ++prev_count;
                        continue;
                    } else if (prev == part and std::holds_alternative<Scalar>(part)) {
                        ++prev_count;
                        continue;
                    }
                }

                if (prev_count > 0) {
                    auto prev_visit = util::Overload{
                        [&](const Scalar& scalar) {
                            switch (scalar) {
                            case Scalar::Number: return std::format("<number> x {},\n", prev_count);
                            case Scalar::String: return std::format("<string> x {},\n", prev_count);
                            case Scalar::Bool: return std::format("<bool> x {},\n", prev_count);
                            case Scalar::Null: return std::format("<null> x {},\n", prev_count);
                            default: [[unlikely]] std::abort();
                            }
                        },
                        [&](const auto&) {
                            error("[octave-ndjson] Repeated object can't be non-scalar."
                                  " This is programmer's fault. Report this to the issue tracker.");
                            return std::string{ "<error>" };
                        },
                    };
                    auto prev_str = std::visit(prev_visit, prev);

                    buffer.append(traversal.size(), '\t');
                    buffer += prev_str;

                    if (std::holds_alternative<Scalar>(part)) {
                        prev       = part;
                        prev_count = 1;
                        continue;
                    }

                    prev_count = 0;
                }

                auto cont = std::visit(visit, part);
                prev      = part;

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
