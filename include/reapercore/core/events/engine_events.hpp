#pragma once

#include "reapercore/core/events/event.hpp"

#include <cstdint>
#include <string_view>

namespace reapercore
{
    struct Application_Started_Event final : Event
    {
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "application.started";
        }
    };

    struct Backend_Tick_Event final : Event
    {
        std::uint64_t tick_index{};

        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "backend.tick";
        }
    };

    struct Shutdown_Requested_Event final : Event
    {
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "application.shutdown_requested";
        }
    };

    struct Application_Stopping_Event final : Event
    {
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "application.stopping";
        }
    };
}
