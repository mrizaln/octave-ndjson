#pragma once

#include <string>
#include <variant>
#include <vector>

namespace octave_ndjson
{
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
        bool root_is_object() const noexcept;

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
        bool is_same(const Schema& other, bool dynamic_array) const noexcept;

        /**
         * @brief Stringify the schema.
         *
         * @return The stringified schema.
         *
         * Useful for debugging.
         */
        std::string stringify(bool dynamic_array) const;

    private:
        std::vector<Part> m_parts;
    };
}
