#include "reapercore/core/folder_system/folder_system_manager.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <array>
#include <memory>

namespace reapercore
{
    bool Folder_System_Manager::initialize()
    {
        PWSTR roaming_app_data_raw{};
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &roaming_app_data_raw);

        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> roaming_app_data(
            roaming_app_data_raw,
            &CoTaskMemFree);

        if (FAILED(result) || roaming_app_data == nullptr ||
            *roaming_app_data == L'\0')
        {
            return false;
        }

        m_root = std::filesystem::path(roaming_app_data.get()) / "ReaperCore";

        m_core = m_root / "Core";
        m_core_logs = m_core / "Logs";
        m_core_settings = m_core / "Settings";
        m_core_cache = m_core / "Cache";
        m_core_data = m_core / "Data";

        m_backend = m_root / "Backend";
        m_backend_data = m_backend / "Data";

        m_frontend = m_root / "Frontend";
        m_frontend_themes = m_frontend / "Themes";
        m_frontend_layouts = m_frontend / "Layouts";

        m_lua = m_root / "ReaperCore_Lua_System";
        m_lua_scripts = m_lua / "Scripts";
        m_lua_modules = m_lua / "Modules";
        m_lua_data = m_lua / "Data";
        m_lua_logs = m_lua / "Logs";

        const std::array directories{
            m_root,
            m_core,
            m_core_logs,
            m_core_settings,
            m_core_cache,
            m_core_data,
            m_backend,
            m_backend_data,
            m_frontend,
            m_frontend_themes,
            m_frontend_layouts,
            m_lua,
            m_lua_scripts,
            m_lua_modules,
            m_lua_data,
            m_lua_logs
        };

        for (const auto& directory : directories)
        {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
                return false;
        }

        return true;
    }

    const std::filesystem::path& Folder_System_Manager::root() const noexcept { return m_root; }
    const std::filesystem::path& Folder_System_Manager::core() const noexcept { return m_core; }
    const std::filesystem::path& Folder_System_Manager::core_logs() const noexcept { return m_core_logs; }
    const std::filesystem::path& Folder_System_Manager::core_settings() const noexcept { return m_core_settings; }
    const std::filesystem::path& Folder_System_Manager::core_cache() const noexcept { return m_core_cache; }
    const std::filesystem::path& Folder_System_Manager::core_data() const noexcept { return m_core_data; }
    const std::filesystem::path& Folder_System_Manager::backend() const noexcept { return m_backend; }
    const std::filesystem::path& Folder_System_Manager::backend_data() const noexcept { return m_backend_data; }
    const std::filesystem::path& Folder_System_Manager::frontend() const noexcept { return m_frontend; }
    const std::filesystem::path& Folder_System_Manager::frontend_themes() const noexcept { return m_frontend_themes; }
    const std::filesystem::path& Folder_System_Manager::frontend_layouts() const noexcept { return m_frontend_layouts; }
    const std::filesystem::path& Folder_System_Manager::lua() const noexcept { return m_lua; }
    const std::filesystem::path& Folder_System_Manager::lua_scripts() const noexcept { return m_lua_scripts; }
    const std::filesystem::path& Folder_System_Manager::lua_modules() const noexcept { return m_lua_modules; }
    const std::filesystem::path& Folder_System_Manager::lua_data() const noexcept { return m_lua_data; }
    const std::filesystem::path& Folder_System_Manager::lua_logs() const noexcept { return m_lua_logs; }
}
