#include "reapercore/application.hpp"

#include <chrono>
#include <thread>

namespace reapercore
{
    application::application(const HMODULE module) noexcept :
        m_module(module)
    {
    }

    DWORD application::run()
    {
        if (!m_logger.initialize())
        {
            return ERROR_OPEN_FAILED;
        }

        m_logger.write(log_level::info, "ReaperCore loaded successfully.");
        m_logger.write(log_level::info, "Press END to unload cleanly.");

        while (m_running.load(std::memory_order_relaxed))
        {
            if ((GetAsyncKeyState(VK_END) & 1) != 0)
            {
                request_stop();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        m_logger.write(log_level::info, "ReaperCore shutting down.");
        m_logger.shutdown();
        FreeLibraryAndExitThread(m_module, EXIT_SUCCESS);
        return EXIT_SUCCESS;
    }

    void application::request_stop() noexcept
    {
        m_running.store(false, std::memory_order_relaxed);
    }
}
