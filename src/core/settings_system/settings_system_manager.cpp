#include "reapercore/core/settings_system/settings_system_manager.hpp"
#include "reapercore/core/file_system/file_system_manager.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    std::string trim(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }
}

namespace reapercore
{
    bool Settings_System_Manager::initialize(
        File_System_Manager& files,
        std::filesystem::path settings_file)
    {
        m_files = &files;
        m_settings_file = std::move(settings_file);
        install_defaults();
        return load();
    }

    void Settings_System_Manager::install_defaults()
    {
        std::scoped_lock lock(m_mutex);
        m_values = {
            {"backend.tick_ms", "50"},
            {"logging.minimum_level", "info"},
            {"logging.asynchronous", "true"},
            {"logging.console", "true"},
            {"logging.file", "true"},
            {"logging.debugger", "true"},
            {"logging.memory", "true"},
            {"logging.rotate_on_start", "false"},
            {"logging.queue_capacity", "8192"},
            {"logging.memory_capacity", "1024"},
            {"logging.maximum_file_mb", "8"},
            {"logging.retained_files", "5"},
            {"lua.enabled", "true"},
            {"lua.auto_scan", "true"}
        };
    }

    bool Settings_System_Manager::load()
    {
        if (m_files == nullptr)
            return false;

        const auto content = m_files->read_text(m_settings_file);
        if (!content)
            return save();

        std::unordered_map<std::string, std::string> loaded;
        std::istringstream stream(*content);
        std::string line;
        while (std::getline(stream, line))
        {
            line = trim(std::move(line));
            if (line.empty() || line.front() == '#' || line.front() == ';')
                continue;

            const auto separator = line.find('=');
            if (separator == std::string::npos)
                continue;

            auto key = trim(line.substr(0, separator));
            auto value = trim(line.substr(separator + 1));
            if (!key.empty())
                loaded[std::move(key)] = std::move(value);
        }

        std::scoped_lock lock(m_mutex);
        for (auto& [key, value] : loaded)
            m_values[std::move(key)] = std::move(value);
        return true;
    }

    bool Settings_System_Manager::save() const
    {
        if (m_files == nullptr)
            return false;

        std::vector<std::pair<std::string, std::string>> ordered;
        {
            std::scoped_lock lock(m_mutex);
            ordered.assign(m_values.begin(), m_values.end());
        }
        std::sort(ordered.begin(), ordered.end());

        std::ostringstream stream;
        stream << "# ReaperCore settings\n";
        for (const auto& [key, value] : ordered)
            stream << key << '=' << value << '\n';
        return m_files->write_text_atomic(m_settings_file, stream.str());
    }

    void Settings_System_Manager::set(std::string key, std::string value)
    {
        std::scoped_lock lock(m_mutex);
        m_values[std::move(key)] = std::move(value);
    }

    void Settings_System_Manager::set_bool(std::string key, const bool value)
    {
        set(std::move(key), value ? "true" : "false");
    }

    void Settings_System_Manager::set_int(std::string key, const int value)
    {
        set(std::move(key), std::to_string(value));
    }

    bool Settings_System_Manager::erase(const std::string_view key)
    {
        std::scoped_lock lock(m_mutex);
        return m_values.erase(std::string(key)) != 0;
    }

    bool Settings_System_Manager::contains(const std::string_view key) const
    {
        std::scoped_lock lock(m_mutex);
        return m_values.contains(std::string(key));
    }

    std::string Settings_System_Manager::get(
        const std::string_view key,
        const std::string_view fallback) const
    {
        std::scoped_lock lock(m_mutex);
        const auto found = m_values.find(std::string(key));
        return found == m_values.end() ? std::string(fallback) : found->second;
    }

    bool Settings_System_Manager::get_bool(
        const std::string_view key,
        const bool fallback) const
    {
        const auto value = get(key);
        if (value == "true" || value == "1" || value == "yes" || value == "on")
            return true;
        if (value == "false" || value == "0" || value == "no" || value == "off")
            return false;
        return fallback;
    }

    int Settings_System_Manager::get_int(
        const std::string_view key,
        const int fallback) const
    {
        const auto value = get(key);
        int result{};
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), result);
        return error == std::errc{} && end == value.data() + value.size()
            ? result
            : fallback;
    }

    std::unordered_map<std::string, std::string>
        Settings_System_Manager::snapshot() const
    {
        std::scoped_lock lock(m_mutex);
        return m_values;
    }

    const std::filesystem::path& Settings_System_Manager::settings_file() const noexcept
    {
        return m_settings_file;
    }
}
