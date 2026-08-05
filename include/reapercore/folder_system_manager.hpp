#pragma once

#include <filesystem>

namespace reapercore
{
    class Folder_System_Manager final
    {
    public:
        bool initialize();

        [[nodiscard]] const std::filesystem::path& root() const noexcept;
        [[nodiscard]] const std::filesystem::path& logs() const noexcept;
        [[nodiscard]] const std::filesystem::path& settings() const noexcept;
        [[nodiscard]] const std::filesystem::path& cache() const noexcept;
        [[nodiscard]] const std::filesystem::path& scripts() const noexcept;

    private:
        std::filesystem::path m_root;
        std::filesystem::path m_logs;
        std::filesystem::path m_settings;
        std::filesystem::path m_cache;
        std::filesystem::path m_scripts;
    };
}
