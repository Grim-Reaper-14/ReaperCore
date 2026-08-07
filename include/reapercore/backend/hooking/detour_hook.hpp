#pragma once

#include <cstdint>

namespace reapercore
{
    class Logging_Manager;

    class Detour_Hook final
    {
    public:
        bool create(
            Logging_Manager& logging,
            const char* name,
            std::uintptr_t target,
            void* detour) noexcept;
        bool enable() noexcept;
        void disable() noexcept;
        void destroy() noexcept;

        [[nodiscard]] bool created() const noexcept;
        [[nodiscard]] bool enabled() const noexcept;
        [[nodiscard]] void* original() const noexcept;

        template <typename T>
        [[nodiscard]] T original_as() const noexcept
        {
            return reinterpret_cast<T>(m_original);
        }

    private:
        Logging_Manager* m_logging{};
        const char* m_name{};
        void* m_target{};
        void* m_original{};
        bool m_created{};
        bool m_enabled{};
    };
}
