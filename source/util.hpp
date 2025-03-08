#pragma once

#include <dtl_modern/dtl_modern.hpp>

#include <format>
#include <iostream>

namespace util
{
    /**
     * @brief Helper struct for creating overloaded lambdas.
     *
     * @tparam Fs The lambdas to be included in the overload.
     *
     * Useful for `std::visit`.
     */
    template <typename... Fs>
    struct Overload : Fs...
    {
        using Fs::operator()...;
    };

    /**
     * @class StringSplitter
     *
     * @brief On-demand string splitter.
     */
    class StringSplitter
    {
    public:
        StringSplitter(std::string_view str, char delim) noexcept
            : m_str{ str }
            , m_delim{ delim }
        {
        }

        /**
         * @brief Get the next split string.
         *
         * @return The next split string, or `std::nullopt` if there is no more string to split.
         */
        std::optional<std::string_view> next() noexcept
        {
            if (m_idx >= m_str.size()) {
                return std::nullopt;
            }

            while (m_idx <= m_str.size() and m_str[m_idx] == m_delim) {
                ++m_idx;
            }

            auto it = std::ranges::find(m_str | std::views::drop(m_idx), m_delim);

            if (it == m_str.end()) {
                auto res = m_str.substr(m_idx);
                m_idx    = m_str.size();    // mark end of line
                return res;
            }

            auto pos = static_cast<std::size_t>(it - m_str.begin());
            auto res = m_str.substr(m_idx, pos - m_idx);
            m_idx    = pos + 1;

            return res;
        }

    private:
        std::string_view m_str;
        std::size_t      m_idx   = 0;
        char             m_delim = '\n';
    };

    /**
     * @brief Log to stdout.
     *
     * For debugging purposes.
     */
    template <typename... Args>
    inline void log(std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        std::cout << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }

    /**
     * @brief Split a string by a delimiter.
     *
     * @param str The string to be split.
     * @param delim The delimiter.
     *
     * @return A vector of split strings.
     *
     * The split is done at once. Use `StringSplitter` for on-demand split.
     */
    inline std::vector<std::string_view> split(std::string_view str, char delim) noexcept
    {
        auto res = std::vector<std::string_view>{};

        auto splitter = StringSplitter{ str, delim };
        while (auto line = splitter.next()) {
            res.push_back(line.value());
        }

        return res;
    }

    /**
     * @brief Create a formatted diff string between two strings.
     *
     * @param lhs Left-hand side string.
     * @param rhs Right-hand side string.
     *
     * @return A pair of formatted diff strings (each string for each side).
     */
    inline std::pair<std::string, std::string> create_diff(
        std::string_view lhs,
        std::string_view rhs
    ) noexcept
    {
        auto lhs_lines = split(lhs, '\n');
        auto rhs_lines = split(rhs, '\n');

        auto lines_diff = dtl_modern::diff(lhs_lines, rhs_lines);

        auto  buffer       = std::pair<std::string, std::string>{};
        auto& buffer_left  = buffer.first;
        auto& buffer_right = buffer.second;

        for (auto&& [line, type] : lines_diff.m_ses.get()) {
            switch (type.m_type) {
            case dtl_modern::SesEdit::Common:
                buffer_left  += std::format("{}\n", line);
                buffer_right += std::format("{}\n", line);
                break;
            case dtl_modern::SesEdit::Delete:
                buffer_left += std::format("\033[1;31m{}\033[0m\n", line);
                break;
            case dtl_modern::SesEdit::Add:    //
                buffer_right += std::format("\033[1;32m{}\033[0m\n", line);
                break;
            }
        }

        return buffer;
    }
}
