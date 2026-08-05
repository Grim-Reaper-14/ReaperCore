#include "reapercore/settings_system_manager.hpp"
#include "reapercore/file_system_manager.hpp"

#include <charconv>
#include <sstream>

namespace reapercore
{
    bool Settings_System_Manager::initialize(File_System_Manager& files, std::filesystem::path settings_file)
    {
        m_files = &files;
        m_settings_file = std::move(settings_file);
        return load();
    }

    bool Settings_System_Manager::load()
    {
        if (m_files == nullptr) return false;
        const auto content = m_files->read_text(m_settings_file);
        if (!content)
        {
            set("backend.tick_ms", "50");
            set("console.enabled", "true");
            return save();
        }

        std::unordered_map<std::string, std::string> loaded;
        std::istringstream stream(*content);
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos) continue;
            loaded[line.substr(0, separator)] = line.substr(separator + 1);
        }

        std::scoped_lock lock(m_mutex);
        m_values = std::move(loaded);
        return true;
    }

    bool Settings_System_Manager::save() const
    {
        if (m_files == nullptr) return false;
        std::ostringstream stream;
        std::scoped_lock lock(m_mutex);
        for (const auto& [key, value] : m_values)
            stream << key << '=' << value << '\n';
        return m_files->write_text(m_settings_file, stream.str());
    }

    void Settings_System_Manager::set(std::string key, std::string value)
    {
        std::scoped_lock lock(m_mutex);
        m_values[std::move(key)] = std::move(value);
    }

    std::string Settings_System_Manager::get(std::string_view key, std::string_view fallback) const
    {
        std::scoped_lock lock(m_mutex);
        const auto found = m_values.find(std::string(key));
        return found == m_values.end() ? std::string(fallback) : found->second;
    }

    bool Settings_System_Manager::get_bool(std::string_view key, bool fallback) const
    {
        const auto value = get(key);
        if (value == "true" || value == "1") return true;
        if (value == "false" || value == "0") return false;
        return fallback;
    }

    int Settings_System_Manager::get_int(std::string_view key, int fallback) const
    {
        const auto value = get(key);
        int result{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        return error == std::errc{} && end == value.data() + value.size() ? result : fallback;
    }
}
