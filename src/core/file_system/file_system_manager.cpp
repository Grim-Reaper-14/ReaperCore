#include "reapercore/core/file_system/file_system_manager.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace reapercore
{
    bool File_System_Manager::exists(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        return std::filesystem::exists(path, error) && !error;
    }

    bool File_System_Manager::is_file(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) && !error;
    }

    bool File_System_Manager::is_directory(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        return std::filesystem::is_directory(path, error) && !error;
    }

    bool File_System_Manager::create_directory(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        return !error;
    }

    std::optional<std::string> File_System_Manager::read_text(
        const std::filesystem::path& path) const
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return std::nullopt;

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    bool File_System_Manager::write_text(
        const std::filesystem::path& path,
        const std::string_view content) const
    {
        std::error_code error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;

        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        return stream.good();
    }

    bool File_System_Manager::write_text_atomic(
        const std::filesystem::path& path,
        const std::string_view content) const
    {
        auto temporary = path;
        temporary += ".tmp";
        if (!write_text(temporary, content))
            return false;

        std::error_code error;
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (!error)
            return true;

        std::filesystem::remove(temporary, error);
        return false;
    }

    bool File_System_Manager::append_text(
        const std::filesystem::path& path,
        const std::string_view content) const
    {
        std::error_code error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;

        std::ofstream stream(path, std::ios::binary | std::ios::app);
        if (!stream)
            return false;
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        return stream.good();
    }

    bool File_System_Manager::copy(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const bool overwrite) const noexcept
    {
        std::error_code error;
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            return false;

        const auto options = overwrite
            ? std::filesystem::copy_options::overwrite_existing
            : std::filesystem::copy_options::none;
        std::filesystem::copy_file(source, destination, options, error);
        return !error;
    }

    bool File_System_Manager::remove(const std::filesystem::path& path) const noexcept
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        return !error;
    }

    std::vector<std::filesystem::path> File_System_Manager::list_files(
        const std::filesystem::path& directory,
        const std::string_view extension,
        const bool recursive) const
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error) || error)
            return files;

        const auto matches = [extension](const std::filesystem::path& path)
        {
            return extension.empty() || path.extension().string() == extension;
        };

        if (recursive)
        {
            for (std::filesystem::recursive_directory_iterator iterator(
                    directory,
                    std::filesystem::directory_options::skip_permission_denied,
                    error), end;
                 iterator != end;
                 iterator.increment(error))
            {
                if (error)
                {
                    error.clear();
                    continue;
                }
                if (iterator->is_regular_file(error) && !error && matches(iterator->path()))
                    files.push_back(iterator->path());
            }
        }
        else
        {
            for (std::filesystem::directory_iterator iterator(
                    directory,
                    std::filesystem::directory_options::skip_permission_denied,
                    error), end;
                 iterator != end;
                 iterator.increment(error))
            {
                if (error)
                {
                    error.clear();
                    continue;
                }
                if (iterator->is_regular_file(error) && !error && matches(iterator->path()))
                    files.push_back(iterator->path());
            }
        }

        std::sort(files.begin(), files.end());
        return files;
    }
}
