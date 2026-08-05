#include "reapercore/core/lua/reapercore_lua_system.hpp"
#include "reapercore/core/file_system/file_system_manager.hpp"
#include "reapercore/core/folder_system/folder_system_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"
#include "reapercore/core/settings_system/settings_system_manager.hpp"

namespace reapercore
{
    bool ReaperCore_Lua_System::initialize(
        File_System_Manager& files,
        Folder_System_Manager& folders,
        Settings_System_Manager& settings,
        Logging_Manager& logging)
    {
        m_enabled = settings.get_bool("lua.enabled", true);
        if (!m_enabled)
        {
            logging.info("lua", "ReaperCore_Lua_System is disabled by settings.");
            m_initialized = true;
            return true;
        }

        Lua_Runtime_Context context;
        context.scripts_directory = folders.lua_scripts();
        context.modules_directory = folders.lua_modules();
        context.data_directory = folders.lua_data();

        if (!m_manager.initialize(files, logging, std::move(context)))
            return false;

        if (settings.get_bool("lua.auto_scan", true))
            static_cast<void>(m_manager.discover_scripts());

        m_initialized = true;
        logging.info("lua", "ReaperCore_Lua_System initialized.");
        return true;
    }

    void ReaperCore_Lua_System::tick()
    {
        if (m_initialized && m_enabled)
            m_manager.tick();
    }

    void ReaperCore_Lua_System::shutdown() noexcept
    {
        if (!m_initialized)
            return;
        if (m_enabled)
            m_manager.shutdown();
        m_initialized = false;
    }

    LuaCore_Manager& ReaperCore_Lua_System::manager() noexcept
    {
        return m_manager;
    }

    const LuaCore_Manager& ReaperCore_Lua_System::manager() const noexcept
    {
        return m_manager;
    }

    bool ReaperCore_Lua_System::enabled() const noexcept
    {
        return m_enabled;
    }
}
