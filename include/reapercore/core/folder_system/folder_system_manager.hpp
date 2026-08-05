#pragma once

#include <filesystem>

namespace reapercore
{
    class Folder_System_Manager final
    {
    public:
        bool initialize();

        [[nodiscard]] const std::filesystem::path& root() const noexcept;

        [[nodiscard]] const std::filesystem::path& core() const noexcept;
        [[nodiscard]] const std::filesystem::path& core_logs() const noexcept;
        [[nodiscard]] const std::filesystem::path& core_settings() const noexcept;
        [[nodiscard]] const std::filesystem::path& core_cache() const noexcept;
        [[nodiscard]] const std::filesystem::path& core_data() const noexcept;

        [[nodiscard]] const std::filesystem::path& backend() const noexcept;
        [[nodiscard]] const std::filesystem::path& backend_data() const noexcept;

        [[nodiscard]] const std::filesystem::path& frontend() const noexcept;
        [[nodiscard]] const std::filesystem::path& frontend_themes() const noexcept;
        [[nodiscard]] const std::filesystem::path& frontend_layouts() const noexcept;

        [[nodiscard]] const std::filesystem::path& lua() const noexcept;
        [[nodiscard]] const std::filesystem::path& lua_scripts() const noexcept;
        [[nodiscard]] const std::filesystem::path& lua_modules() const noexcept;
        [[nodiscard]] const std::filesystem::path& lua_data() const noexcept;
        [[nodiscard]] const std::filesystem::path& lua_logs() const noexcept;

    private:
        std::filesystem::path m_root;
        std::filesystem::path m_core;
        std::filesystem::path m_core_logs;
        std::filesystem::path m_core_settings;
        std::filesystem::path m_core_cache;
        std::filesystem::path m_core_data;
        std::filesystem::path m_backend;
        std::filesystem::path m_backend_data;
        std::filesystem::path m_frontend;
        std::filesystem::path m_frontend_themes;
        std::filesystem::path m_frontend_layouts;
        std::filesystem::path m_lua;
        std::filesystem::path m_lua_scripts;
        std::filesystem::path m_lua_modules;
        std::filesystem::path m_lua_data;
        std::filesystem::path m_lua_logs;
    };
}
