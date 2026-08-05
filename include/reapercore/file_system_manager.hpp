#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace reapercore
{
    class File_System_Manager final
    {
    public:
        [[nodiscard]] bool exists(const std::filesystem::path& path) const noexcept;
        [[nodiscard]] bool create_directory(const std::filesystem::path& path) const noexcept;
        [[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) const;
        [[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view content) const;
        [[nodiscard]] bool remove(const std::filesystem::path& path) const noexcept;
    };
}
