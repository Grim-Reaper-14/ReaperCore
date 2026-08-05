#pragma once

#include "reapercore/backend.hpp"
#include "reapercore/file_system_manager.hpp"
#include "reapercore/folder_system_manager.hpp"
#include "reapercore/logger.hpp"
#include "reapercore/settings_system_manager.hpp"

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
        [[nodiscard]] Backend& backend() noexcept;
        [[nodiscard]] logger& log() noexcept;

    private:
        Folder_System_Manager m_folders;
        File_System_Manager m_files;
        Settings_System_Manager m_settings;
        logger m_logger;
        Backend m_backend;
        bool m_initialized{};
    };
}
