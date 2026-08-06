#pragma once

#include "reapercore/backend/gta/gta_pointers.hpp"

#include <cstddef>
#include <cstdint>

namespace reapercore
{
    class Logging_Manager;

    class GTA_Runtime final
    {
    public:
        bool initialize(Logging_Manager& logging) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] bool ready_for_natives() const noexcept;
        [[nodiscard]] std::uintptr_t module_base() const noexcept;
        [[nodiscard]] std::size_t module_size() const noexcept;
        [[nodiscard]] GTA_Pointers& pointers() noexcept;
        [[nodiscard]] const GTA_Pointers& pointers() const noexcept;

    private:
        Logging_Manager* m_logging{};
        GTA_Pointers m_pointers;
        std::uintptr_t m_module_base{};
        std::size_t m_module_size{};
        bool m_initialized{};
    };
}
