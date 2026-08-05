#include "reapercore/application.hpp"

namespace reapercore
{
    application::application(const HMODULE module) noexcept :
        m_module(module)
    {
    }

    DWORD application::run()
    {
        if (!m_core.initialize())
            return ERROR_INITIALIZATION_FAILED;

        m_core.log().write(log_level::info, "ReaperCore loaded successfully.");
        m_core.log().write(log_level::info, "Press END to unload cleanly.");

        m_core.run();
        m_core.shutdown();

        FreeLibraryAndExitThread(m_module, EXIT_SUCCESS);
        return EXIT_SUCCESS;
    }
}
