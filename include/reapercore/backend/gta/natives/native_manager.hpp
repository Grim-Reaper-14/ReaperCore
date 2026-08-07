#pragma once

#include "reapercore/backend/gta/natives/native_context.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reapercore
{
    class GTA_Pointers;
    class Logging_Manager;

    class Native_Manager final
    {
    public:
        bool initialize(
            Logging_Manager& logging,
            const GTA_Pointers& pointers) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] bool handlers_cached() const noexcept;
        [[nodiscard]] std::size_t handler_count() const noexcept;
        [[nodiscard]] Native_Handler handler(std::size_t index) const noexcept;

    private:
        bool cache_handlers(
            Logging_Manager& logging,
            const GTA_Pointers& pointers) noexcept;

        std::vector<Native_Handler> m_handlers;
        bool m_initialized{};
        bool m_handlers_cached{};
    };
}
