#pragma once

#include "reapercore/backend/d3d12/d3d12_backend.hpp"
#include "reapercore/backend/gta/gta_runtime.hpp"
#include "reapercore/backend/hooking/hook_registry.hpp"
#include "reapercore/backend/imgui/dx12_texture_registry.hpp"
#include "reapercore/backend/imgui/imgui_layer.hpp"
#include "reapercore/backend/imgui/win32_dx12_backend.hpp"
#include "reapercore/backend/renderer/renderer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace reapercore
{
    class Event_Manager;
    class Logging_Manager;
    class ReaperCore_Lua_System;
    class Settings_System_Manager;
    class Task_Manager;

    struct Backend_ImGui_DX12_Attach_Info
    {
        HWND window{};
        ID3D12Device* device{};
        ID3D12CommandQueue* command_queue{};
        int frames_in_flight{3};
        DXGI_FORMAT rtv_format{DXGI_FORMAT_R8G8B8A8_UNORM};
        DXGI_FORMAT dsv_format{DXGI_FORMAT_UNKNOWN};
        std::uint32_t srv_descriptor_capacity{64};
    };

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

        bool attach_imgui_dx12(
            const Backend_ImGui_DX12_Attach_Info& info) noexcept;
        void detach_imgui_dx12() noexcept;
        bool begin_imgui_frame() noexcept;
        bool render_imgui_frame(
            const ImGui_DX12_Frame_Context& frame) noexcept;
        bool handle_imgui_window_message(
            HWND window,
            UINT message,
            WPARAM word_parameter,
            LPARAM long_parameter) noexcept;

        [[nodiscard]] std::optional<ImGui_DX12_Texture_Handle>
            register_imgui_texture(
                ID3D12Resource& resource,
                const D3D12_SHADER_RESOURCE_VIEW_DESC* description = nullptr) noexcept;
        bool unregister_imgui_texture(ImGui_DX12_Texture_Id id) noexcept;
        [[nodiscard]] std::optional<ImGui_DX12_Texture_Handle>
            find_imgui_texture(ImGui_DX12_Texture_Id id) const noexcept;
        [[nodiscard]] ImGui_DX12_Texture_Metrics
            imgui_texture_metrics() const noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] GTA_Runtime& gta() noexcept;
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
        GTA_Runtime m_gta;
        D3D12_Backend m_d3d12;
        ImGui_Layer m_imgui;
        ImGui_Win32_DX12_Backend m_imgui_backend;
        ImGui_DX12_Texture_Registry m_imgui_textures;
        Renderer m_renderer;
        Hook_Registry m_hooks;
        mutable std::mutex m_imgui_attachment_mutex;
        bool m_imgui_owns_d3d12_attachment{};
        std::atomic_bool m_running{false};
        std::chrono::milliseconds m_tick_interval{50};
        std::size_t m_main_thread_task_budget{64};
        std::uint64_t m_tick_index{};
    };
}
