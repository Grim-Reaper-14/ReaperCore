#include "reapercore/core.hpp"

namespace reapercore
{
    bool Core::initialize()
    {
        if (!m_folders.initialize()) return false;
        if (!m_logger.initialize()) return false;
        if (!m_settings.initialize(m_files, m_folders.settings() / "ReaperCore.ini")) return false;
        if (!m_backend.initialize(m_logger, m_settings)) return false;

        m_initialized = true;
        m_logger.write(log_level::info, "Core initialized.");
        return true;
    }

    void Core::run()
    {
        if (m_initialized)
            m_backend.run();
    }

    void Core::shutdown() noexcept
    {
        if (!m_initialized) return;
        m_backend.shutdown();
        m_settings.save();
        m_logger.write(log_level::info, "Core shutting down.");
        m_logger.shutdown();
        m_initialized = false;
    }

    Folder_System_Manager& Core::folders() noexcept { return m_folders; }
    File_System_Manager& Core::files() noexcept { return m_files; }
    Settings_System_Manager& Core::settings() noexcept { return m_settings; }
    Backend& Core::backend() noexcept { return m_backend; }
    logger& Core::log() noexcept { return m_logger; }
}
