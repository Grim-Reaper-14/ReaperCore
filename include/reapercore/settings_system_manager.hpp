#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace reapercore
{
    class File_System_Manager;

    class Settings_System_Manager final
    {
    public:
        bool initialize(File_System_Manager& files, std::filesystem::path settings_file);
        bool load();
        bool save() const;

        void set(std::string key, std::string value);
        [[nodiscard]] std::string get(std::string_view key, std::string_view fallback = {}) const;
        [[nodiscard]] bool get_bool(std::string_view key, bool fallback = false) const;
        [[nodiscard]] int get_int(std::string_view key, int fallback = 0) const;

    private:
        File_System_Manager* m_files{};
        std::filesystem::path m_settings_file;
        mutable std::mutex m_mutex;
        std::unordered_map<std::string, std::string> m_values;
    };
}
