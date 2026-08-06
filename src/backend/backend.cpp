#include "reapercore/backend/backend.hpp"
#include "reapercore/core/events/engine_events.hpp"
#include "reapercore/core/events/event_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"
#include "reapercore/core/lua/reapercore_lua_system.hpp"
#include "reapercore/core/settings_system/settings_system_manager.hpp"

#include <Windows.h>

#include <string>
#include <thread>

namespace reapercore
{
    bool Backend::initialize(
        Logging_Manager& logging,
        Settings_System_Manager& settings,
        ReaperCore_Lua_System& lua,
        Event_Manager& events) noexcept
    {
        m_logging = &logging;
        m_settings = &settings;
        m_lua = &lua;
        m_events = &events;
        m_tick_index = 0;

        const int tick_ms = settings.get_int("backend.tick_ms", 50);
        m_tick_interval = std::chrono::milliseconds(tick_ms > 0 ? tick_ms : 50);
        m_running.store(true, std::memory_order_release);

        m_logging->info("backend", "Backend initialized.", {
            {"tick_ms", std::to_string(m_tick_interval.count())}
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

            if (m_events != nullptr)
            {
                m_events->process_deferred();
                m_events->publish(Backend_Tick_Event{++m_tick_index});
            }

            if (m_lua != nullptr)
                m_lua->tick();

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

        if (m_logging != nullptr)
            m_logging->info("backend", "Backend stopped.", {
                {"ticks", std::to_string(m_tick_index)}
            });

        m_events = nullptr;
        m_lua = nullptr;
        m_settings = nullptr;
        m_logging = nullptr;
    }

    bool Backend::running() const noexcept
    {
        return m_running.load(std::memory_order_acquire);
    }

    D3D12_Backend& Backend::d3d12() noexcept
    {
        return m_d3d12;
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
