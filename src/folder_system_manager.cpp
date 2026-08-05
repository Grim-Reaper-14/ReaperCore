#include "reapercore/folder_system_manager.hpp"

#include <cstdlib>

namespace reapercore
{
    bool Folder_System_Manager::initialize()
    {
        const char* appdata = std::getenv("APPDATA");
        if (appdata == nullptr || *appdata == '\0')
        {
            return false;
        }

        m_root = std::filesystem::path(appdata) / "ReaperCore";
        m_logs = m_root / "Logs";
        m_settings = m_root / "Settings";
        m_cache = m_root / "Cache";
        m_scripts = m_root / "Scripts";

        std::error_code error;
        std::filesystem::create_directories(m_logs, error);
        if (error) return false;
        std::filesystem::create_directories(m_settings, error);
        if (error) return false;
        std::filesystem::create_directories(m_cache, error);
        if (error) return false;
        std::filesystem::create_directories(m_scripts, error);
        return !error;
    }

    const std::filesystem::path& Folder_System_Manager::root() const noexcept { return m_root; }
    const std::filesystem::path& Folder_System_Manager::logs() const noexcept { return m_logs; }
    const std::filesystem::path& Folder_System_Manager::settings() const noexcept { return m_settings; }
    const std::filesystem::path& Folder_System_Manager::cache() const noexcept { return m_cache; }
    const std::filesystem::path& Folder_System_Manager::scripts() const noexcept { return m_scripts; }
}
