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
        bool initialize(
            File_System_Manager& files,
            std::filesystem::path settings_file);
        bool load();
        bool save() const;

        void set(std::string key, std::string value);
        void set_bool(std::string key, bool value);
        void set_int(std::string key, int value);
        [[nodiscard]] bool erase(std::string_view key);
        [[nodiscard]] bool contains(std::string_view key) const;

        [[nodiscard]] std::string get(
            std::string_view key,
            std::string_view fallback = {}) const;
        [[nodiscard]] bool get_bool(
            std::string_view key,
            bool fallback = false) const;
        [[nodiscard]] int get_int(
            std::string_view key,
            int fallback = 0) const;

        [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const;
        [[nodiscard]] const std::filesystem::path& settings_file() const noexcept;

    private:
        void install_defaults();

        File_System_Manager* m_files{};
        std::filesystem::path m_settings_file;
        mutable std::mutex m_mutex;
        std::unordered_map<std::string, std::string> m_values;
    };
}
