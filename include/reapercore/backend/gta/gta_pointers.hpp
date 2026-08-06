#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reapercore
{
    class Logging_Manager;

    class GTA_Pointers final
    {
    public:
        struct Resolution final
        {
            std::string name;
            std::uintptr_t address{};
            bool required{};
        };

        bool initialize(
            Logging_Manager& logging,
            std::uintptr_t module_base,
            std::size_t module_size) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] bool ready_for_natives() const noexcept;
        [[nodiscard]] const std::vector<Resolution>& resolutions() const noexcept;

        [[nodiscard]] std::uintptr_t script_threads() const noexcept;
        [[nodiscard]] std::uintptr_t script_programs() const noexcept;
        [[nodiscard]] std::uintptr_t script_globals() const noexcept;
        [[nodiscard]] std::uintptr_t run_script_threads() const noexcept;
        [[nodiscard]] std::uintptr_t init_native_tables() const noexcept;

    private:
        Logging_Manager* m_logging{};
        std::vector<Resolution> m_resolutions;
        std::uintptr_t m_script_threads{};
        std::uintptr_t m_script_programs{};
        std::uintptr_t m_script_globals{};
        std::uintptr_t m_run_script_threads{};
        std::uintptr_t m_init_native_tables{};
        bool m_initialized{};
    };
}
