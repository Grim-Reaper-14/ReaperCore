#pragma once

#include "reapercore/backend/backend.hpp"
#include "reapercore/core/events/event_manager.hpp"
#include "reapercore/core/file_system/file_system_manager.hpp"
#include "reapercore/core/folder_system/folder_system_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"
#include "reapercore/core/lua/reapercore_lua_system.hpp"
#include "reapercore/core/settings_system/settings_system_manager.hpp"

namespace reapercore
{
    class Core final
    {
    public:
        bool initialize();
        void run();
        void shutdown() noexcept;

        [[nodiscard]] Folder_System_Manager& folders() noexcept;
        [[nodiscard]] File_System_Manager& files() noexcept;
        [[nodiscard]] Settings_System_Manager& settings() noexcept;
        [[nodiscard]] Logging_Manager& logging() noexcept;
        [[nodiscard]] Event_Manager& events() noexcept;
        [[nodiscard]] ReaperCore_Lua_System& lua() noexcept;
        [[nodiscard]] Backend& backend() noexcept;

    private:
        Folder_System_Manager m_folders;
        File_System_Manager m_files;
        Settings_System_Manager m_settings;
        Logging_Manager m_logging;
        Event_Manager m_events;
        ReaperCore_Lua_System m_lua;
        Backend m_backend;
        bool m_initialized{};
    };
}
