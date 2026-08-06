#pragma once

#include "reapercore/backend/d3d12/d3d12_backend.hpp"
#include "reapercore/backend/hooking/hook_registry.hpp"
#include "reapercore/backend/imgui/imgui_layer.hpp"
#include "reapercore/backend/imgui/win32_dx12_backend.hpp"
#include "reapercore/backend/renderer/renderer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace reapercore
{
    class Event_Manager;
    class Logging_Manager;
    class ReaperCore_Lua_System;
    class Settings_System_Manager;
    class Task_Manager;

    class Backend final
    {
    public:
        bool initialize(
            Logging_Manager& logging,
            Settings_System_Manager& settings,
            ReaperCore_Lua_System& lua,
            Event_Manager& events,
            Task_Manager& tasks) noexcept;
        void run();
        void request_stop() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] D3D12_Backend& d3d12() noexcept;
        [[nodiscard]] ImGui_Layer& imgui() noexcept;
        [[nodiscard]] ImGui_Win32_DX12_Backend& imgui_backend() noexcept;
        [[nodiscard]] Renderer& renderer() noexcept;
        [[nodiscard]] Hook_Registry& hooks() noexcept;

    private:
        Logging_Manager* m_logging{};
        Settings_System_Manager* m_settings{};
        ReaperCore_Lua_System* m_lua{};
        Event_Manager* m_events{};
        Task_Manager* m_tasks{};
        D3D12_Backend m_d3d12;
        ImGui_Layer m_imgui;
        ImGui_Win32_DX12_Backend m_imgui_backend;
        Renderer m_renderer;
        Hook_Registry m_hooks;
        std::atomic_bool m_running{false};
        std::chrono::milliseconds m_tick_interval{50};
        std::size_t m_main_thread_task_budget{64};
        std::uint64_t m_tick_index{};
    };
}
