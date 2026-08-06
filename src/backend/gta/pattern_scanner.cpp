#include "reapercore/backend/gta/pattern_scanner.hpp"

#include <charconv>
#include <cctype>
#include <limits>

namespace reapercore
{
    namespace
    {
        [[nodiscard]] bool is_separator(const char value) noexcept
        {
            return std::isspace(static_cast<unsigned char>(value)) != 0;
        }
    }

    Pattern_Scanner::Pattern_Scanner(
        const std::uintptr_t base,
        const std::size_t size) noexcept :
        m_base(base),
        m_size(size)
    {
    }

    std::optional<std::uintptr_t>
    Pattern_Scanner::find(const std::string_view pattern) const noexcept
    {
        const auto parsed = parse(pattern);
        if (!parsed || parsed->empty() || m_base == 0 || m_size < parsed->size())
            return std::nullopt;

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(m_base);
        const std::size_t last_start = m_size - parsed->size();

        for (std::size_t offset = 0; offset <= last_start; ++offset)
        {
            bool matched = true;
            for (std::size_t index = 0; index < parsed->size(); ++index)
            {
                const auto& expected = (*parsed)[index];
                if (!expected.wildcard && bytes[offset + index] != expected.value)
                {
                    matched = false;
                    break;
                }
            }

            if (matched)
                return m_base + offset;
        }

        return std::nullopt;
    }

    std::optional<std::vector<Pattern_Scanner::Byte>>
    Pattern_Scanner::parse(const std::string_view pattern) noexcept
    {
        std::vector<Byte> result;
        std::size_t cursor = 0;

        while (cursor < pattern.size())
        {
            while (cursor < pattern.size() && is_separator(pattern[cursor]))
                ++cursor;

            if (cursor >= pattern.size())
                break;

            const std::size_t token_begin = cursor;
            while (cursor < pattern.size() && !is_separator(pattern[cursor]))
                ++cursor;

            const std::string_view token = pattern.substr(
                token_begin,
                cursor - token_begin);

            if (token == "?" || token == "??")
            {
                result.push_back(Byte{0, true});
                continue;
            }

            if (token.size() != 2)
                return std::nullopt;

            unsigned int value = 0;
            const auto conversion = std::from_chars(
                token.data(),
                token.data() + token.size(),
                value,
                16);

            if (conversion.ec != std::errc{} ||
                conversion.ptr != token.data() + token.size() ||
                value > std::numeric_limits<std::uint8_t>::max())
            {
                return std::nullopt;
            }

            result.push_back(Byte{
                static_cast<std::uint8_t>(value),
                false});
        }

        if (result.empty())
            return std::nullopt;

        return result;
    }
}
