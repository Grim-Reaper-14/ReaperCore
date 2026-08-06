#include "reapercore/core/core/core.hpp"
#include "reapercore/core/events/engine_events.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace
{
    std::size_t bounded_size(
        const int value,
        const int minimum,
        const int maximum,
        const std::size_t fallback) noexcept
    {
        if (value < minimum || value > maximum)
            return fallback;
        return static_cast<std::size_t>(value);
    }
}

namespace reapercore
{
    bool Core::initialize()
    {
        if (m_initialized)
            return true;

        if (!m_folders.initialize())
            return false;

        if (!m_settings.initialize(
                m_files,
                m_folders.core_settings() / "ReaperCore.ini"))
        {
            return false;
        }

        Logging_Config logging_config;
        logging_config.directory = m_folders.core_logs();
        logging_config.minimum_level = parse_log_level(
            m_settings.get("logging.minimum_level", "info"));
        logging_config.asynchronous = m_settings.get_bool("logging.asynchronous", true);
        logging_config.enable_console = m_settings.get_bool("logging.console", true);
        logging_config.enable_file = m_settings.get_bool("logging.file", true);
        logging_config.enable_debugger = m_settings.get_bool("logging.debugger", true);
        logging_config.enable_memory_buffer = m_settings.get_bool("logging.memory", true);
        logging_config.rotate_on_start = m_settings.get_bool("logging.rotate_on_start", false);
        logging_config.queue_capacity = bounded_size(
            m_settings.get_int("logging.queue_capacity", 8192),
            128,
            1'048'576,
            8192);
        logging_config.memory_capacity = bounded_size(
            m_settings.get_int("logging.memory_capacity", 1024),
            16,
            100'000,
            1024);
        const auto maximum_file_mb = bounded_size(
            m_settings.get_int("logging.maximum_file_mb", 8),
            1,
            1024,
            8);
        logging_config.maximum_file_size_bytes = maximum_file_mb * 1024U * 1024U;
        logging_config.retained_file_count = bounded_size(
            m_settings.get_int("logging.retained_files", 5),
            0,
            100,
            5);

        if (!m_logging.initialize(std::move(logging_config)))
            return false;

        if (!m_lua.initialize(m_files, m_folders, m_settings, m_logging))
        {
            m_logging.error("core", "ReaperCore_Lua_System failed to initialize.");
            m_logging.shutdown();
            return false;
        }

        if (!m_backend.initialize(m_logging, m_settings, m_lua, m_events))
        {
            m_logging.error("core", "Backend failed to initialize.");
            m_lua.shutdown();
            m_logging.shutdown();
            return false;
        }

        m_initialized = true;
        m_events.publish(Application_Started_Event{});
        m_logging.info("core", "Core initialized.", {
            {"root", m_folders.root().string()},
            {"settings", m_settings.settings_file().string()}
        });
        return true;
    }

    void Core::run()
    {
        if (m_initialized)
            m_backend.run();
    }

    void Core::shutdown() noexcept
    {
        if (!m_initialized)
            return;

        m_events.publish(Application_Stopping_Event{});
        m_backend.shutdown();
        m_events.process_deferred();
        m_events.clear();
        m_lua.shutdown();
        static_cast<void>(m_settings.save());
        m_logging.info("core", "Core shutting down.");
        m_logging.shutdown();
        m_initialized = false;
    }

    Folder_System_Manager& Core::folders() noexcept { return m_folders; }
    File_System_Manager& Core::files() noexcept { return m_files; }
    Settings_System_Manager& Core::settings() noexcept { return m_settings; }
    Logging_Manager& Core::logging() noexcept { return m_logging; }
    Event_Manager& Core::events() noexcept { return m_events; }
    ReaperCore_Lua_System& Core::lua() noexcept { return m_lua; }
    Backend& Core::backend() noexcept { return m_backend; }
}
