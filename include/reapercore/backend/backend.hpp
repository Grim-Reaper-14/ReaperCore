#pragma once

#include "reapercore/backend/d3d12/d3d12_backend.hpp"
#include "reapercore/backend/hooking/hook_registry.hpp"
#include "reapercore/backend/renderer/renderer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace reapercore
{
    class Event_Manager;
    class Logging_Manager;
    class ReaperCore_Lua_System;
    class Settings_System_Manager;

    class Backend final
    {
    public:
        bool initialize(
            Logging_Manager& logging,
            Settings_System_Manager& settings,
            ReaperCore_Lua_System& lua,
            Event_Manager& events) noexcept;
        void run();
        void request_stop() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] D3D12_Backend& d3d12() noexcept;
        [[nodiscard]] Renderer& renderer() noexcept;
        [[nodiscard]] Hook_Registry& hooks() noexcept;

    private:
        Logging_Manager* m_logging{};
        Settings_System_Manager* m_settings{};
        ReaperCore_Lua_System* m_lua{};
        Event_Manager* m_events{};
        D3D12_Backend m_d3d12;
        Renderer m_renderer;
        Hook_Registry m_hooks;
        std::atomic_bool m_running{false};
        std::chrono::milliseconds m_tick_interval{50};
        std::uint64_t m_tick_index{};
    };
}
