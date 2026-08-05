#include "reapercore/core/logging/logging_manager.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <utility>

namespace
{
    thread_local std::vector<reapercore::Log_Field> thread_context;

    std::string timestamp_text(const std::chrono::system_clock::time_point value)
    {
        const auto time = std::chrono::system_clock::to_time_t(value);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            value.time_since_epoch()) % 1000;
        std::tm local{};
        localtime_s(&local, &time);
        std::ostringstream stream;
        stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
               << '.' << std::setfill('0') << std::setw(3) << milliseconds.count();
        return stream.str();
    }

    std::string source_name(const char* file)
    {
        if (file == nullptr)
            return {};
        const std::string value(file);
        const auto position = value.find_last_of("/\\");
        return position == std::string::npos ? value : value.substr(position + 1);
    }

    std::string json_escape(const std::string_view value)
    {
        std::ostringstream stream;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (character < 0x20)
                {
                    stream << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<int>(character)
                           << std::dec;
                }
                else
                {
                    stream << static_cast<char>(character);
                }
                break;
            }
        }
        return stream.str();
    }

    WORD console_color(const reapercore::Log_Level level) noexcept
    {
        switch (level)
        {
        case reapercore::Log_Level::trace: return FOREGROUND_INTENSITY;
        case reapercore::Log_Level::debug: return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case reapercore::Log_Level::info: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        case reapercore::Log_Level::warning: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case reapercore::Log_Level::error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
        case reapercore::Log_Level::critical: return BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case reapercore::Log_Level::off: return 0;
        }
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }

    bool at_least(const reapercore::Log_Level value, const reapercore::Log_Level minimum) noexcept
    {
        return value != reapercore::Log_Level::off
            && static_cast<unsigned>(value) >= static_cast<unsigned>(minimum)
            && minimum != reapercore::Log_Level::off;
    }
}

namespace reapercore
{
    std::string_view to_string(const Log_Level level) noexcept
    {
        switch (level)
        {
        case Log_Level::trace: return "TRACE";
        case Log_Level::debug: return "DEBUG";
        case Log_Level::info: return "INFO";
        case Log_Level::warning: return "WARN";
        case Log_Level::error: return "ERROR";
        case Log_Level::critical: return "CRITICAL";
        case Log_Level::off: return "OFF";
        }
        return "UNKNOWN";
    }

    Log_Level parse_log_level(std::string_view value, const Log_Level fallback) noexcept
    {
        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        if (normalized == "trace") return Log_Level::trace;
        if (normalized == "debug") return Log_Level::debug;
        if (normalized == "info") return Log_Level::info;
        if (normalized == "warn" || normalized == "warning") return Log_Level::warning;
        if (normalized == "error") return Log_Level::error;
        if (normalized == "critical" || normalized == "fatal") return Log_Level::critical;
        if (normalized == "off") return Log_Level::off;
        return fallback;
    }

    Logging_Manager::Scoped_Timer::Scoped_Timer(
        Logging_Manager& manager,
        std::string channel,
        std::string name,
        const std::source_location source) noexcept :
        m_manager(&manager),
        m_channel(std::move(channel)),
        m_name(std::move(name)),
        m_started(std::chrono::steady_clock::now()),
        m_source(source)
    {
    }

    Logging_Manager::Scoped_Timer::~Scoped_Timer()
    {
        if (m_manager == nullptr)
            return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_started);
        m_manager->debug(m_channel, m_name, {
            {"duration_us", std::to_string(elapsed.count())}
        }, m_source);
    }

    Logging_Manager::Scoped_Timer::Scoped_Timer(Scoped_Timer&& other) noexcept :
        m_manager(std::exchange(other.m_manager, nullptr)),
        m_channel(std::move(other.m_channel)),
        m_name(std::move(other.m_name)),
        m_started(other.m_started),
        m_source(other.m_source)
    {
    }

    Logging_Manager::Scoped_Timer& Logging_Manager::Scoped_Timer::operator=(Scoped_Timer&& other) noexcept
    {
        if (this != &other)
        {
            m_manager = std::exchange(other.m_manager, nullptr);
            m_channel = std::move(other.m_channel);
            m_name = std::move(other.m_name);
            m_started = other.m_started;
            m_source = other.m_source;
        }
        return *this;
    }

    Logging_Manager::Scoped_Context::Scoped_Context(
        Logging_Manager& manager,
        const std::initializer_list<Log_Field> fields) :
        m_manager(&manager),
        m_previous_size(manager.push_thread_context(fields))
    {
    }

    Logging_Manager::Scoped_Context::~Scoped_Context()
    {
        if (m_manager != nullptr)
            m_manager->restore_thread_context(m_previous_size);
    }

    Logging_Manager::Scoped_Context::Scoped_Context(Scoped_Context&& other) noexcept :
        m_manager(std::exchange(other.m_manager, nullptr)),
        m_previous_size(other.m_previous_size)
    {
    }

    Logging_Manager::Scoped_Context& Logging_Manager::Scoped_Context::operator=(Scoped_Context&& other) noexcept
    {
        if (this != &other)
        {
            if (m_manager != nullptr)
                m_manager->restore_thread_context(m_previous_size);
            m_manager = std::exchange(other.m_manager, nullptr);
            m_previous_size = other.m_previous_size;
        }
        return *this;
    }

    Logging_Manager::~Logging_Manager()
    {
        shutdown();
    }

    bool Logging_Manager::initialize(Logging_Config config)
    {
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        config.queue_capacity = std::max<std::size_t>(config.queue_capacity, 1);
        config.memory_capacity = std::max<std::size_t>(config.memory_capacity, 1);
        m_config = std::move(config);
        m_minimum_level.store(m_config.minimum_level, std::memory_order_relaxed);

        std::error_code error;
        std::filesystem::create_directories(m_config.directory, error);
        if (error)
            return false;

        m_file_path = m_config.directory / m_config.file_name;

        if (m_config.enable_console)
        {
            if (GetConsoleWindow() == nullptr)
                m_console_owned = AllocConsole() != FALSE;
            if (GetConsoleWindow() != nullptr)
            {
                FILE* stream{};
                freopen_s(&stream, "CONOUT$", "w", stdout);
                freopen_s(&stream, "CONOUT$", "w", stderr);
                SetConsoleTitleW(L"ReaperCore Logging Manager");
                SetConsoleOutputCP(CP_UTF8);
                CONSOLE_SCREEN_BUFFER_INFO info{};
                if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
                    m_console_default_attributes = info.wAttributes;
            }
        }

        {
            std::scoped_lock lock(m_output_mutex);
            if (m_config.rotate_on_start && std::filesystem::exists(m_file_path, error))
                rotate_files_unlocked();

            if (m_config.enable_file)
            {
                m_file.open(m_file_path, std::ios::binary | std::ios::app);
                if (!m_file)
                    return false;
                m_current_file_size = static_cast<std::size_t>(
                    std::filesystem::file_size(m_file_path, error));
                if (error)
                    m_current_file_size = 0;
            }
        }

        m_accepting.store(true, std::memory_order_release);
        m_initialized.store(true, std::memory_order_release);

        try
        {
            if (m_config.asynchronous)
                m_worker = std::thread(&Logging_Manager::worker_loop, this);
        }
        catch (...)
        {
            m_accepting.store(false, std::memory_order_release);
            m_initialized.store(false, std::memory_order_release);
            return false;
        }

        info("logging", "Logging_Manager initialized.", {
            {"file", m_file_path.string()},
            {"async", m_config.asynchronous ? "true" : "false"}
        });
        return true;
    }

    void Logging_Manager::shutdown() noexcept
    {
        if (!m_initialized.load(std::memory_order_acquire))
            return;

        try
        {
            info("logging", "Logging_Manager shutting down.");
            m_accepting.store(false, std::memory_order_release);
            m_queue_condition.notify_all();
            if (m_worker.joinable())
                m_worker.join();

            std::scoped_lock lock(m_output_mutex);
            if (m_file.is_open())
            {
                m_file.flush();
                m_file.close();
            }
        }
        catch (...)
        {
        }

        if (m_console_owned)
        {
            FreeConsole();
            m_console_owned = false;
        }

        m_initialized.store(false, std::memory_order_release);
    }

    void Logging_Manager::flush()
    {
        if (m_config.asynchronous)
        {
            std::unique_lock lock(m_flush_mutex);
            m_flush_condition.wait(lock, [this]
            {
                return m_pending_records.load(std::memory_order_acquire) == 0;
            });
        }
        std::scoped_lock lock(m_output_mutex);
        if (m_file.is_open())
            m_file.flush();
    }

    void Logging_Manager::rotate_now()
    {
        std::scoped_lock lock(m_output_mutex);
        rotate_files_unlocked();
        if (m_config.enable_file)
        {
            m_file.open(m_file_path, std::ios::binary | std::ios::app);
            m_current_file_size = 0;
        }
    }

    void Logging_Manager::log(
        const Log_Level level,
        const std::string_view channel,
        const std::string_view message,
        const std::initializer_list<Log_Field> fields,
        const std::source_location source)
    {
        if (!m_accepting.load(std::memory_order_acquire) || !should_log(level, channel))
            return;

        Log_Record record;
        record.sequence = m_next_sequence.fetch_add(1, std::memory_order_relaxed);
        record.timestamp = std::chrono::system_clock::now();
        record.level = level;
        record.channel = channel.empty() ? "core" : std::string(channel);
        record.message = std::string(message);
        record.thread_id = std::this_thread::get_id();
        record.source = source;
        record.fields = thread_context;
        record.fields.insert(record.fields.end(), fields.begin(), fields.end());
        m_accepted_records.fetch_add(1, std::memory_order_relaxed);

        if (m_config.asynchronous)
            enqueue(std::move(record));
        else
            dispatch(record);
    }

    void Logging_Manager::trace(const std::string_view channel, const std::string_view message,
        const std::initializer_list<Log_Field> fields, const std::source_location source)
    { log(Log_Level::trace, channel, message, fields, source); }

    void Logging_Manager::debug(const std::string_view channel, const std::string_view message,
        const std::initializer_list<Log_Field> fields, const std::source_location source)
    { log(Log_Level::debug, channel, message, fields, source); }

    void Logging_Manager::info(const std::string_view channel, const std::string_view message,
        const std::initializer_list<Log_Field> fields, const std::source_location source)
    { log(Log_Level::info, channel, message, fields, source); }

    void Logging_Manager::warning(const std::string_view channel, const std::string_view message,
        const std::initializer_list<Log_Field> fields, const std::source_location source)
    { log(Log_Level::warning, channel, message, fields, source); }

    void Logging_Manager::error(const std::string_view channel, const std::string_view message,
        const std::initializer_list<Log_Field> fields, const std::source_location source)
    { log(Log_Level::error, channel, message, fields, source); }

    void Logging_Manager::critical(const std::string_view channel, const std::string_view message,
        const std::initializer_list<Log_Field> fields, const std::source_location source)
    { log(Log_Level::critical, channel, message, fields, source); }

    void Logging_Manager::log_exception(
        const std::string_view channel,
        const std::exception& exception,
        const std::string_view message,
        const std::source_location source)
    {
        error(channel, message, {
            {"exception", exception.what()}
        }, source);
    }

    void Logging_Manager::hexdump(
        const std::string_view channel,
        const std::string_view label,
        const std::span<const std::byte> data,
        const std::size_t maximum_bytes,
        const std::source_location source)
    {
        const auto count = std::min(data.size(), maximum_bytes);
        std::ostringstream stream;
        stream << label << " [" << count << '/' << data.size() << " bytes]";
        for (std::size_t offset = 0; offset < count; offset += 16)
        {
            stream << "\n" << std::hex << std::setfill('0') << std::setw(6) << offset << ": ";
            const auto line_end = std::min(count, offset + 16);
            for (std::size_t index = offset; index < line_end; ++index)
            {
                stream << std::setw(2)
                       << std::to_integer<unsigned int>(data[index]) << ' ';
            }
        }
        debug(channel, stream.str(), {}, source);
    }

    Logging_Manager::Scoped_Timer Logging_Manager::scoped_timer(
        const std::string_view channel,
        const std::string_view name,
        const std::source_location source)
    {
        return Scoped_Timer(*this, std::string(channel), std::string(name), source);
    }

    Logging_Manager::Scoped_Context Logging_Manager::scoped_context(
        const std::initializer_list<Log_Field> fields)
    {
        return Scoped_Context(*this, fields);
    }

    void Logging_Manager::set_minimum_level(const Log_Level level) noexcept
    {
        m_minimum_level.store(level, std::memory_order_relaxed);
    }

    Log_Level Logging_Manager::minimum_level() const noexcept
    {
        return m_minimum_level.load(std::memory_order_relaxed);
    }

    void Logging_Manager::set_channel_level(std::string channel, const Log_Level level)
    {
        std::scoped_lock lock(m_channel_mutex);
        m_channel_levels[std::move(channel)] = level;
    }

    void Logging_Manager::clear_channel_level(const std::string_view channel)
    {
        std::scoped_lock lock(m_channel_mutex);
        m_channel_levels.erase(std::string(channel));
    }

    void Logging_Manager::clear_channel_levels()
    {
        std::scoped_lock lock(m_channel_mutex);
        m_channel_levels.clear();
    }

    Logging_Manager::Listener_Token Logging_Manager::add_listener(Listener listener)
    {
        if (!listener)
            return 0;
        const auto token = m_next_listener_token.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(m_listener_mutex);
        m_listeners.emplace(token, std::move(listener));
        return token;
    }

    bool Logging_Manager::remove_listener(const Listener_Token token)
    {
        std::scoped_lock lock(m_listener_mutex);
        return m_listeners.erase(token) != 0;
    }

    std::vector<Log_Record> Logging_Manager::recent_records(
        const std::size_t maximum_records) const
    {
        std::scoped_lock lock(m_recent_mutex);
        const auto count = maximum_records == 0
            ? m_recent_records.size()
            : std::min(maximum_records, m_recent_records.size());
        return std::vector<Log_Record>(m_recent_records.end() - static_cast<std::ptrdiff_t>(count),
            m_recent_records.end());
    }

    Logging_Metrics Logging_Manager::metrics() const
    {
        Logging_Metrics result;
        result.accepted_records = m_accepted_records.load(std::memory_order_relaxed);
        result.written_records = m_written_records.load(std::memory_order_relaxed);
        result.dropped_records = m_dropped_records.load(std::memory_order_relaxed);
        result.rotated_files = m_rotated_files.load(std::memory_order_relaxed);
        result.bytes_written = m_bytes_written.load(std::memory_order_relaxed);
        result.listener_failures = m_listener_failures.load(std::memory_order_relaxed);
        {
            std::scoped_lock lock(m_queue_mutex);
            result.queue_depth = m_queue.size();
        }
        result.peak_queue_depth = m_peak_queue_depth.load(std::memory_order_relaxed);
        {
            std::scoped_lock lock(m_recent_mutex);
            result.recent_record_count = m_recent_records.size();
        }
        return result;
    }

    std::filesystem::path Logging_Manager::file_path() const
    {
        std::scoped_lock lock(m_output_mutex);
        return m_file_path;
    }

    bool Logging_Manager::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }

    bool Logging_Manager::should_log(
        const Log_Level level,
        const std::string_view channel) const
    {
        Log_Level minimum = m_minimum_level.load(std::memory_order_relaxed);
        {
            std::scoped_lock lock(m_channel_mutex);
            const auto found = m_channel_levels.find(std::string(channel));
            if (found != m_channel_levels.end())
                minimum = found->second;
        }
        return at_least(level, minimum);
    }

    void Logging_Manager::enqueue(Log_Record record)
    {
        {
            std::scoped_lock lock(m_queue_mutex);
            if (m_queue.size() >= m_config.queue_capacity)
            {
                if (!m_config.drop_oldest_on_overflow)
                {
                    m_dropped_records.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                m_queue.pop_front();
                m_pending_records.fetch_sub(1, std::memory_order_relaxed);
                m_dropped_records.fetch_add(1, std::memory_order_relaxed);
            }
            m_queue.push_back(std::move(record));
            m_pending_records.fetch_add(1, std::memory_order_release);
            const auto depth = m_queue.size();
            auto peak = m_peak_queue_depth.load(std::memory_order_relaxed);
            while (depth > peak && !m_peak_queue_depth.compare_exchange_weak(
                peak, depth, std::memory_order_relaxed))
            {
            }
        }
        m_queue_condition.notify_one();
    }

    void Logging_Manager::worker_loop()
    {
        for (;;)
        {
            Log_Record record;
            {
                std::unique_lock lock(m_queue_mutex);
                m_queue_condition.wait(lock, [this]
                {
                    return !m_queue.empty()
                        || !m_accepting.load(std::memory_order_acquire);
                });
                if (m_queue.empty() && !m_accepting.load(std::memory_order_acquire))
                    break;
                record = std::move(m_queue.front());
                m_queue.pop_front();
            }

            try
            {
                dispatch(record);
            }
            catch (...)
            {
                m_dropped_records.fetch_add(1, std::memory_order_relaxed);
            }

            if (m_pending_records.fetch_sub(1, std::memory_order_acq_rel) == 1)
                m_flush_condition.notify_all();
        }
    }

    void Logging_Manager::dispatch(const Log_Record& record)
    {
        const auto line = m_config.file_format == Log_File_Format::json_lines
            ? format_json(record)
            : format_text(record);
        const auto bytes = line.size() + 1;

        {
            std::scoped_lock lock(m_output_mutex);
            rotate_if_required(bytes);

            if (m_config.enable_console && GetConsoleWindow() != nullptr)
            {
                const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
                if (m_config.color_console)
                    SetConsoleTextAttribute(output, console_color(record.level));
                std::cout << line << '\n';
                if (m_config.color_console)
                    SetConsoleTextAttribute(output, m_console_default_attributes);
            }

            if (m_config.enable_debugger)
            {
                const auto debugger_line = line + "\n";
                OutputDebugStringA(debugger_line.c_str());
            }

            if (m_file.is_open())
            {
                m_file << line << '\n';
                m_current_file_size += bytes;
                m_bytes_written.fetch_add(bytes, std::memory_order_relaxed);
                if (m_config.flush_on_error && record.level >= Log_Level::error)
                    m_file.flush();
            }
        }

        if (m_config.enable_memory_buffer)
        {
            std::scoped_lock lock(m_recent_mutex);
            m_recent_records.push_back(record);
            while (m_recent_records.size() > m_config.memory_capacity)
                m_recent_records.pop_front();
        }

        std::vector<Listener> listeners;
        {
            std::scoped_lock lock(m_listener_mutex);
            listeners.reserve(m_listeners.size());
            for (const auto& [token, listener] : m_listeners)
            {
                static_cast<void>(token);
                listeners.push_back(listener);
            }
        }

        for (const auto& listener : listeners)
        {
            try
            {
                listener(record);
            }
            catch (...)
            {
                m_listener_failures.fetch_add(1, std::memory_order_relaxed);
            }
        }

        m_written_records.fetch_add(1, std::memory_order_relaxed);
    }

    std::string Logging_Manager::format_text(const Log_Record& record) const
    {
        std::ostringstream stream;
        stream << '[' << timestamp_text(record.timestamp) << "] ["
               << to_string(record.level) << "] [" << record.channel << "] [#"
               << record.sequence << ']';
        if (m_config.include_thread_id)
            stream << " [tid=" << std::hash<std::thread::id>{}(record.thread_id) << ']';
        stream << ' ' << record.message;
        for (const auto& field : record.fields)
            stream << " {" << field.key << '=' << field.value << '}';
        if (m_config.include_source && record.source.file_name()[0] != '\0')
        {
            stream << " (" << source_name(record.source.file_name())
                   << ':' << record.source.line() << ')';
        }
        return stream.str();
    }

    std::string Logging_Manager::format_json(const Log_Record& record) const
    {
        std::ostringstream stream;
        stream << "{\"sequence\":" << record.sequence
               << ",\"timestamp\":\"" << json_escape(timestamp_text(record.timestamp))
               << "\",\"level\":\"" << to_string(record.level)
               << "\",\"channel\":\"" << json_escape(record.channel)
               << "\",\"message\":\"" << json_escape(record.message) << '"';
        if (m_config.include_thread_id)
            stream << ",\"thread_id\":" << std::hash<std::thread::id>{}(record.thread_id);
        if (m_config.include_source && record.source.file_name()[0] != '\0')
        {
            stream << ",\"source\":{\"file\":\""
                   << json_escape(source_name(record.source.file_name()))
                   << "\",\"line\":" << record.source.line()
                   << ",\"function\":\""
                   << json_escape(record.source.function_name()) << "\"}";
        }
        stream << ",\"fields\":{";
        for (std::size_t index = 0; index < record.fields.size(); ++index)
        {
            if (index != 0)
                stream << ',';
            stream << '"' << json_escape(record.fields[index].key) << "\":\""
                   << json_escape(record.fields[index].value) << '"';
        }
        stream << "}}";
        return stream.str();
    }

    void Logging_Manager::rotate_if_required(const std::size_t incoming_bytes)
    {
        if (!m_config.enable_file || m_config.maximum_file_size_bytes == 0
            || m_current_file_size + incoming_bytes <= m_config.maximum_file_size_bytes)
        {
            return;
        }

        rotate_files_unlocked();
        m_file.open(m_file_path, std::ios::binary | std::ios::app);
        m_current_file_size = 0;
    }

    void Logging_Manager::rotate_files_unlocked()
    {
        if (m_file.is_open())
        {
            m_file.flush();
            m_file.close();
        }

        std::error_code error;
        if (m_config.retained_file_count == 0)
        {
            std::filesystem::remove(m_file_path, error);
            m_rotated_files.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        for (std::size_t index = m_config.retained_file_count; index > 1; --index)
        {
            const auto destination = std::filesystem::path(
                m_file_path.string() + '.' + std::to_string(index));
            const auto source = std::filesystem::path(
                m_file_path.string() + '.' + std::to_string(index - 1));
            std::filesystem::remove(destination, error);
            error.clear();
            if (std::filesystem::exists(source, error) && !error)
            {
                std::filesystem::rename(source, destination, error);
                error.clear();
            }
        }

        const auto first = std::filesystem::path(m_file_path.string() + ".1");
        std::filesystem::remove(first, error);
        error.clear();
        if (std::filesystem::exists(m_file_path, error) && !error)
            std::filesystem::rename(m_file_path, first, error);
        m_rotated_files.fetch_add(1, std::memory_order_relaxed);
    }

    std::size_t Logging_Manager::push_thread_context(
        const std::initializer_list<Log_Field> fields)
    {
        const auto previous = thread_context.size();
        thread_context.insert(thread_context.end(), fields.begin(), fields.end());
        return previous;
    }

    void Logging_Manager::restore_thread_context(const std::size_t previous_size) noexcept
    {
        if (previous_size <= thread_context.size())
            thread_context.resize(previous_size);
    }
}
