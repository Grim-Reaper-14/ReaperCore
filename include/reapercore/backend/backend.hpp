#pragma once

#include <atomic>
#include <chrono>

namespace reapercore
{
    class Logging_Manager;
    class ReaperCore_Lua_System;
    class Settings_System_Manager;

    class Backend final
    {
    public:
        bool initialize(
            Logging_Manager& logging,
            Settings_System_Manager& settings,
            ReaperCore_Lua_System& lua) noexcept;
        void run();
        void request_stop() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool running() const noexcept;

    private:
        Logging_Manager* m_logging{};
        Settings_System_Manager* m_settings{};
        ReaperCore_Lua_System* m_lua{};
        std::atomic_bool m_running{false};
        std::chrono::milliseconds m_tick_interval{50};
    };
}
