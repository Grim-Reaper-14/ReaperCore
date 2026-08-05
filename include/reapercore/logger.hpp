#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace reapercore
{
    enum class log_level
    {
        info,
        warning,
        error
    };

    class logger final
    {
    public:
        logger() = default;
        ~logger();

        logger(const logger&) = delete;
        logger& operator=(const logger&) = delete;

        bool initialize();
        void shutdown() noexcept;
        void write(log_level level, std::string_view message);

        [[nodiscard]] const std::filesystem::path& file_path() const noexcept;

    private:
        std::mutex m_mutex;
        std::ofstream m_file;
        std::filesystem::path m_file_path;
        bool m_console_owned{};
    };
}
