#pragma once

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
        [[nodiscard]] std::uintptr_t module_base() const noexcept;
        [[nodiscard]] std::size_t module_size() const noexcept;

    private:
        Logging_Manager* m_logging{};
        std::uintptr_t m_module_base{};
        std::size_t m_module_size{};
        bool m_initialized{};
    };
}
