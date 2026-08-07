#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace reapercore
{
    class Pattern_Scanner final
    {
    public:
        struct Byte final
        {
            std::uint8_t value{};
            bool wildcard{};
        };

        Pattern_Scanner(std::uintptr_t base, std::size_t size) noexcept;

        [[nodiscard]] std::optional<std::uintptr_t>
            find(std::string_view pattern) const noexcept;

    private:
        [[nodiscard]] static std::optional<std::vector<Byte>>
            parse(std::string_view pattern) noexcept;

        std::uintptr_t m_base{};
        std::size_t m_size{};
    };
}
