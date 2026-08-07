#pragma once

#include "reapercore/backend/hooking/detour_hook.hpp"

#include <cstdint>
#include <functional>

namespace reapercore
{
    class GTA_Pointers;
    class Logging_Manager;

    class Game_Thread_Hook final
    {
    public:
        using Tick_Callback = std::function<void()>;

        bool initialize(
            Logging_Manager& logging,
            const GTA_Pointers& pointers,
            Tick_Callback callback) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] bool installed() const noexcept;

    private:
        using Run_Script_Threads = bool (*)(int);

        static bool detour(int ops_to_execute) noexcept;
        bool invoke(int ops_to_execute) noexcept;

        static inline Game_Thread_Hook* s_active{};

        Logging_Manager* m_logging{};
        Detour_Hook m_hook;
        Tick_Callback m_callback;
        bool m_initialized{};
    };
}
