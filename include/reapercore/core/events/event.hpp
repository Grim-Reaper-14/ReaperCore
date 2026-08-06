#pragma once

#include <cstdint>
#include <string_view>

namespace reapercore
{
    enum class Event_Priority : std::uint8_t
    {
        lowest,
        low,
        normal,
        high,
        highest,
        monitor
    };

    enum class Event_Result : std::uint8_t
    {
        continue_dispatch,
        consume
    };

    class Event
    {
    public:
        virtual ~Event() = default;
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    };
}
