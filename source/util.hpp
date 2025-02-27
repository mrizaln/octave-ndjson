#pragma once

#include <dtl_modern/dtl_modern.hpp>

#include <format>
#include <iostream>

namespace util
{
    template <typename... Fs>
    struct Overload : Fs...
    {
        using Fs::operator()...;
    };

    class StringSplitter
    {
    public:
        StringSplitter(std::string_view str, char delim) noexcept
            : m_str{ str }
            , m_delim{ delim }
        {
        }

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

    template <typename... Args>
    inline void log(std::format_string<Args...> fmt, Args&&... args)
    {
        std::cout << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }

    inline std::vector<std::string_view> split(std::string_view str, char delim) noexcept
    {
        auto res = std::vector<std::string_view>{};

        auto j = 0ul;
        while (j < str.size()) {
            while (j != str.size() and str[j] == delim) {
                ++j;
            }

            auto it = std::ranges::find(str | std::views::drop(j), delim);

            if (it == str.end()) {
                res.push_back(str.substr(j));
                break;
            }

            auto pos = static_cast<std::size_t>(it - str.begin());
            res.push_back(str.substr(j, pos - j));

            j = pos + 1;
        }

        return res;
    }

    inline std::pair<std::string, std::string> create_diff(std::string_view lhs, std::string_view rhs)
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
