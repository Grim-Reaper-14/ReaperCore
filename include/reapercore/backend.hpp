#pragma once

#include <atomic>
#include <chrono>

namespace reapercore
{
    class logger;
    class Settings_System_Manager;

    class Backend final
    {
    public:
        bool initialize(logger& log, Settings_System_Manager& settings) noexcept;
        void run();
        void request_stop() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool running() const noexcept;

    private:
        logger* m_logger{};
        Settings_System_Manager* m_settings{};
        std::atomic_bool m_running{false};
        std::chrono::milliseconds m_tick_interval{50};
    };
}
