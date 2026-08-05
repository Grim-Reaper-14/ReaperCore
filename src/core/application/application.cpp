#include "reapercore/core/application/application.hpp"

namespace reapercore
{
    Application::Application(const HMODULE module) noexcept :
        m_module(module)
    {
    }

    DWORD Application::run()
    {
        if (!m_core.initialize())
        {
            return ERROR_DLL_INIT_FAILED;
        }

        m_core.logging().info("application", "ReaperCore loaded successfully.");
        m_core.logging().info("application", "Press END to unload cleanly.");

        m_core.run();
        m_core.shutdown();

        FreeLibraryAndExitThread(m_module, EXIT_SUCCESS);
        return EXIT_SUCCESS;
    }
}
