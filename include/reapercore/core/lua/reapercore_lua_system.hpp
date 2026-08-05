#pragma once

#include "reapercore/core/lua/lua_core_manager.hpp"

namespace reapercore
{
    class File_System_Manager;
    class Folder_System_Manager;
    class Logging_Manager;
    class Settings_System_Manager;

    class ReaperCore_Lua_System final
    {
    public:
        bool initialize(
            File_System_Manager& files,
            Folder_System_Manager& folders,
            Settings_System_Manager& settings,
            Logging_Manager& logging);
        void tick();
        void shutdown() noexcept;

        [[nodiscard]] LuaCore_Manager& manager() noexcept;
        [[nodiscard]] const LuaCore_Manager& manager() const noexcept;
        [[nodiscard]] bool enabled() const noexcept;

    private:
        LuaCore_Manager m_manager;
        bool m_enabled{};
        bool m_initialized{};
    };
}
