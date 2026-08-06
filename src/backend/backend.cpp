#include "reapercore/backend/backend.hpp"
#include "reapercore/core/events/engine_events.hpp"
#include "reapercore/core/events/event_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"
#include "reapercore/core/lua/reapercore_lua_system.hpp"
#include "reapercore/core/settings_system/settings_system_manager.hpp"
#include "reapercore/core/tasks/task_manager.hpp"

#include <Windows.h>

#include <string>
#include <thread>

namespace reapercore
{
    bool Backend::initialize(
        Logging_Manager& logging,
        Settings_System_Manager& settings,
        ReaperCore_Lua_System& lua,
        Event_Manager& events,
        Task_Manager& tasks) noexcept
    {
        if (running())
            return true;

        m_logging = &logging;
        m_settings = &settings;
        m_lua = &lua;
        m_events = &events;
        m_tasks = &tasks;
        m_tick_index = 0;

        const int tick_ms = settings.get_int("backend.tick_ms", 50);
        m_tick_interval = std::chrono::milliseconds(tick_ms > 0 ? tick_ms : 50);

        const int task_budget = settings.get_int(
            "backend.main_thread_tasks_per_tick",
            64);
        m_main_thread_task_budget =
            task_budget > 0 && task_budget <= 4096
                ? static_cast<std::size_t>(task_budget)
                : 64;

        if (!m_hooks.initialize(logging))
        {
            m_tasks = nullptr;
            m_events = nullptr;
            m_lua = nullptr;
            m_settings = nullptr;
            m_logging = nullptr;
            return false;
        }

        if (!m_d3d12.initialize(logging))
        {
            m_hooks.shutdown();
            m_tasks = nullptr;
            m_events = nullptr;
            m_lua = nullptr;
            m_settings = nullptr;
            m_logging = nullptr;
            return false;
        }

        if (!m_imgui.initialize(logging))
        {
            m_d3d12.shutdown();
            m_hooks.shutdown();
            m_tasks = nullptr;
            m_events = nullptr;
            m_lua = nullptr;
            m_settings = nullptr;
            m_logging = nullptr;
            return false;
        }

        if (!m_imgui_backend.initialize(logging, m_imgui))
        {
            m_imgui.shutdown();
            m_d3d12.shutdown();
            m_hooks.shutdown();
            m_tasks = nullptr;
            m_events = nullptr;
            m_lua = nullptr;
            m_settings = nullptr;
            m_logging = nullptr;
            return false;
        }

        if (!m_imgui_textures.initialize(logging, m_d3d12))
        {
            m_imgui_backend.shutdown();
            m_imgui.shutdown();
            m_d3d12.shutdown();
            m_hooks.shutdown();
            m_tasks = nullptr;
            m_events = nullptr;
            m_lua = nullptr;
            m_settings = nullptr;
            m_logging = nullptr;
            return false;
        }

        if (!m_renderer.initialize(logging, m_d3d12))
        {
            m_imgui_textures.shutdown();
            m_imgui_backend.shutdown();
            m_imgui.shutdown();
            m_d3d12.shutdown();
            m_hooks.shutdown();
            m_tasks = nullptr;
            m_events = nullptr;
            m_lua = nullptr;
            m_settings = nullptr;
            m_logging = nullptr;
            return false;
        }

        m_running.store(true, std::memory_order_release);
        m_logging->info("backend", "Backend initialized.", {
            {"tick_ms", std::to_string(m_tick_interval.count())},
            {"main_thread_task_budget", std::to_string(m_main_thread_task_budget)},
            {"imgui_version", std::string(m_imgui.version())}
        });
        return true;
    }

    void Backend::run()
    {
        while (m_running.load(std::memory_order_acquire))
        {
            if ((GetAsyncKeyState(VK_END) & 1) != 0)
            {
                if (m_events != nullptr)
                    m_events->publish(Shutdown_Requested_Event{});
                request_stop();
            }

            if (m_tasks != nullptr)
                m_tasks->process_main_thread(m_main_thread_task_budget);

            if (m_events != nullptr)
            {
                m_events->process_deferred();

                Backend_Tick_Event tick_event;
                tick_event.tick_index = ++m_tick_index;
                m_events->publish(tick_event);
            }

            if (m_lua != nullptr)
                m_lua->tick();

            m_renderer.render_frame();
            std::this_thread::sleep_for(m_tick_interval);
        }
    }

    void Backend::request_stop() noexcept
    {
        m_running.store(false, std::memory_order_release);
    }

    void Backend::shutdown() noexcept
    {
        request_stop();

        if (m_events != nullptr)
            m_events->process_deferred();

        m_renderer.shutdown();
        detach_imgui_dx12();
        m_imgui_textures.shutdown();
        m_imgui_backend.shutdown();
        m_imgui.shutdown();
        m_d3d12.shutdown();
        m_hooks.shutdown();

        if (m_logging != nullptr)
        {
            m_logging->info("backend", "Backend stopped.", {
                {"ticks", std::to_string(m_tick_index)}
            });
        }

        m_tasks = nullptr;
        m_events = nullptr;
        m_lua = nullptr;
        m_settings = nullptr;
        m_logging = nullptr;
    }

    bool Backend::attach_imgui_dx12(
        const Backend_ImGui_DX12_Attach_Info& info) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        if (!running() || info.window == nullptr || info.device == nullptr ||
            info.command_queue == nullptr || info.frames_in_flight <= 0 ||
            info.srv_descriptor_capacity == 0)
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "Invalid high-level ImGui DX12 attachment data.");
            return false;
        }

        if (m_imgui_backend.attached())
        {
            return m_imgui_backend.window() == info.window &&
                m_d3d12.device() == info.device;
        }

        const bool device_was_attached = m_d3d12.device_attached();
        if (device_was_attached && m_d3d12.device() != info.device)
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "ImGui cannot attach to a different active DX12 device.");
            return false;
        }

        D3D12_Device_Attach_Info device_info;
        device_info.device = info.device;
        device_info.srv_descriptor_capacity = info.srv_descriptor_capacity;
        if (!m_d3d12.attach_device(device_info))
            return false;

        m_imgui_owns_d3d12_attachment = !device_was_attached;
        auto& descriptors = m_d3d12.srv_descriptors();

        ImGui_Win32_DX12_Attach_Info backend_info;
        backend_info.window = info.window;
        backend_info.device = info.device;
        backend_info.command_queue = info.command_queue;
        backend_info.srv_descriptor_heap = descriptors.heap();
        backend_info.frames_in_flight = info.frames_in_flight;
        backend_info.rtv_format = info.rtv_format;
        backend_info.dsv_format = info.dsv_format;
        backend_info.descriptor_user_data = &descriptors;
        backend_info.allocate_srv_descriptor =
            &D3D12_Srv_Descriptor_Allocator::imgui_allocate;
        backend_info.free_srv_descriptor =
            &D3D12_Srv_Descriptor_Allocator::imgui_release;

        if (!m_imgui_backend.attach(backend_info))
        {
            if (m_imgui_owns_d3d12_attachment)
                m_d3d12.detach_device();
            m_imgui_owns_d3d12_attachment = false;
            return false;
        }

        if (m_logging != nullptr)
        {
            m_logging->info("imgui", "High-level ImGui DX12 bridge attached.", {
                {"frames_in_flight", std::to_string(info.frames_in_flight)},
                {"srv_descriptor_capacity", std::to_string(info.srv_descriptor_capacity)}
            });
        }
        return true;
    }

    void Backend::detach_imgui_dx12() noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        const bool was_attached = m_imgui_backend.attached();

        m_imgui_textures.clear();
        m_imgui_backend.detach();

        if (m_imgui_owns_d3d12_attachment)
            m_d3d12.detach_device();
        m_imgui_owns_d3d12_attachment = false;

        if (was_attached && m_logging != nullptr)
            m_logging->info("imgui", "High-level ImGui DX12 bridge detached.");
    }

    bool Backend::begin_imgui_frame() noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_backend.begin_frame();
    }

    bool Backend::render_imgui_frame(
        const ImGui_DX12_Frame_Context& frame) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_backend.render(frame);
    }

    bool Backend::handle_imgui_window_message(
        const HWND window,
        const UINT message,
        const WPARAM word_parameter,
        const LPARAM long_parameter) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_backend.handle_window_message(
            window,
            message,
            word_parameter,
            long_parameter);
    }

    std::optional<ImGui_DX12_Texture_Handle>
    Backend::register_imgui_texture(
        ID3D12Resource& resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* description) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        if (!m_imgui_backend.attached())
            return std::nullopt;
        return m_imgui_textures.register_texture(resource, description);
    }

    bool Backend::unregister_imgui_texture(
        const ImGui_DX12_Texture_Id id) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_textures.unregister_texture(id);
    }

    std::optional<ImGui_DX12_Texture_Handle>
    Backend::find_imgui_texture(
        const ImGui_DX12_Texture_Id id) const noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_textures.find(id);
    }

    ImGui_DX12_Texture_Metrics
    Backend::imgui_texture_metrics() const noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_textures.metrics();
    }

    bool Backend::running() const noexcept
    {
        return m_running.load(std::memory_order_acquire);
    }

    D3D12_Backend& Backend::d3d12() noexcept
    {
        return m_d3d12;
    }

    ImGui_Layer& Backend::imgui() noexcept
    {
        return m_imgui;
    }

    ImGui_Win32_DX12_Backend& Backend::imgui_backend() noexcept
    {
        return m_imgui_backend;
    }

    Renderer& Backend::renderer() noexcept
    {
        return m_renderer;
    }

    Hook_Registry& Backend::hooks() noexcept
    {
        return m_hooks;
    }
}
