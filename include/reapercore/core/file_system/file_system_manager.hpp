#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reapercore
{
    class File_System_Manager final
    {
    public:
        [[nodiscard]] bool exists(const std::filesystem::path& path) const noexcept;
        [[nodiscard]] bool is_file(const std::filesystem::path& path) const noexcept;
        [[nodiscard]] bool is_directory(const std::filesystem::path& path) const noexcept;
        [[nodiscard]] bool create_directory(const std::filesystem::path& path) const noexcept;

        [[nodiscard]] std::optional<std::string> read_text(
            const std::filesystem::path& path) const;
        [[nodiscard]] bool write_text(
            const std::filesystem::path& path,
            std::string_view content) const;
        [[nodiscard]] bool write_text_atomic(
            const std::filesystem::path& path,
            std::string_view content) const;
        [[nodiscard]] bool append_text(
            const std::filesystem::path& path,
            std::string_view content) const;

        [[nodiscard]] bool copy(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            bool overwrite = true) const noexcept;
        [[nodiscard]] bool remove(const std::filesystem::path& path) const noexcept;

        [[nodiscard]] std::vector<std::filesystem::path> list_files(
            const std::filesystem::path& directory,
            std::string_view extension = {},
            bool recursive = false) const;
    };
}
