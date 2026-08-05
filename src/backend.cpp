#include "reapercore/backend.hpp"
#include "reapercore/logger.hpp"
#include "reapercore/settings_system_manager.hpp"

#include <Windows.h>
#include <thread>

namespace reapercore
{
    bool Backend::initialize(logger& log, Settings_System_Manager& settings) noexcept
    {
        m_logger = &log;
        m_settings = &settings;
        const int tick_ms = settings.get_int("backend.tick_ms", 50);
        m_tick_interval = std::chrono::milliseconds(tick_ms > 0 ? tick_ms : 50);
        m_running.store(true, std::memory_order_relaxed);
        m_logger->write(log_level::info, "Backend initialized.");
        return true;
    }

    void Backend::run()
    {
        while (m_running.load(std::memory_order_relaxed))
        {
            if ((GetAsyncKeyState(VK_END) & 1) != 0)
                request_stop();

            std::this_thread::sleep_for(m_tick_interval);
        }
    }

    void Backend::request_stop() noexcept
    {
        m_running.store(false, std::memory_order_relaxed);
    }

    void Backend::shutdown() noexcept
    {
        request_stop();
        if (m_logger != nullptr)
            m_logger->write(log_level::info, "Backend stopped.");
    }

    bool Backend::running() const noexcept
    {
        return m_running.load(std::memory_order_relaxed);
    }
}
