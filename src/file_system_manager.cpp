#include "reapercore/file_system_manager.hpp"

#include <fstream>
#include <sstream>

namespace reapercore
{
    bool File_System_Manager::exists(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        return std::filesystem::exists(path, error) && !error;
    }

    bool File_System_Manager::create_directory(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        return !error;
    }

    std::optional<std::string> File_System_Manager::read_text(const std::filesystem::path& path) const
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return std::nullopt;
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    bool File_System_Manager::write_text(const std::filesystem::path& path, std::string_view content) const
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        return stream.good();
    }

    bool File_System_Manager::remove(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        return !error;
    }
}
