#include "reapercore/logger.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    const char* level_name(const reapercore::log_level level) noexcept
    {
        switch (level)
        {
        case reapercore::log_level::info:
            return "INFO";
        case reapercore::log_level::warning:
            return "WARN";
        case reapercore::log_level::error:
            return "ERROR";
        }

        return "UNKNOWN";
    }

    std::string timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);

        std::tm local_time{};
        localtime_s(&local_time, &time);

        std::ostringstream stream;
        stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }
}

namespace reapercore
{
    logger::~logger()
    {
        shutdown();
    }

    bool logger::initialize()
    {
        std::scoped_lock lock(m_mutex);

        if (!GetConsoleWindow())
        {
            m_console_owned = AllocConsole() != FALSE;
        }

        if (GetConsoleWindow())
        {
            FILE* stream{};
            freopen_s(&stream, "CONOUT$", "w", stdout);
            freopen_s(&stream, "CONOUT$", "w", stderr);
            SetConsoleTitleW(L"ReaperCore");
            SetConsoleOutputCP(CP_UTF8);
        }

        const char* appdata = std::getenv("APPDATA");
        const auto root = appdata && *appdata
            ? std::filesystem::path(appdata) / "ReaperCore"
            : std::filesystem::temp_directory_path() / "ReaperCore";

        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            return false;
        }

        m_file_path = root / "ReaperCore.log";
        m_file.open(m_file_path, std::ios::out | std::ios::app);
        return m_file.is_open();
    }

    void logger::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);

        if (m_file.is_open())
        {
            m_file.flush();
            m_file.close();
        }

        if (m_console_owned)
        {
            FreeConsole();
            m_console_owned = false;
        }
    }

    void logger::write(const log_level level, const std::string_view message)
    {
        std::scoped_lock lock(m_mutex);

        const auto line = "[" + timestamp() + "] [" + level_name(level) + "] " + std::string(message);
        std::cout << line << '\n';

        if (m_file.is_open())
        {
            m_file << line << '\n';
            m_file.flush();
        }
    }

    const std::filesystem::path& logger::file_path() const noexcept
    {
        return m_file_path;
    }
}
