#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace reapercore
{
    enum class Log_Level : std::uint8_t
    {
        trace,
        debug,
        info,
        warning,
        error,
        critical,
        off
    };

    enum class Log_File_Format : std::uint8_t
    {
        text,
        json_lines
    };

    struct Log_Field
    {
        std::string key;
        std::string value;
    };

    struct Log_Record
    {
        std::uint64_t sequence{};
        std::chrono::system_clock::time_point timestamp{};
        Log_Level level{Log_Level::info};
        std::string channel{"core"};
        std::string message;
        std::thread::id thread_id{};
        std::source_location source{};
        std::vector<Log_Field> fields;
    };

    struct Logging_Config
    {
        std::filesystem::path directory;
        std::string file_name{"ReaperCore.log"};
        Log_Level minimum_level{Log_Level::info};
        Log_File_Format file_format{Log_File_Format::text};
        bool asynchronous{true};
        bool enable_console{true};
        bool enable_file{true};
        bool enable_debugger{true};
        bool enable_memory_buffer{true};
        bool color_console{true};
        bool include_source{true};
        bool include_thread_id{true};
        bool flush_on_error{true};
        bool rotate_on_start{false};
        bool drop_oldest_on_overflow{true};
        std::size_t queue_capacity{8192};
        std::size_t memory_capacity{1024};
        std::size_t maximum_file_size_bytes{8U * 1024U * 1024U};
        std::size_t retained_file_count{5};
    };

    struct Logging_Metrics
    {
        std::uint64_t accepted_records{};
        std::uint64_t written_records{};
        std::uint64_t dropped_records{};
        std::uint64_t rotated_files{};
        std::uint64_t bytes_written{};
        std::uint64_t listener_failures{};
        std::size_t queue_depth{};
        std::size_t peak_queue_depth{};
        std::size_t recent_record_count{};
    };

    [[nodiscard]] std::string_view to_string(Log_Level level) noexcept;
    [[nodiscard]] Log_Level parse_log_level(
        std::string_view value,
        Log_Level fallback = Log_Level::info) noexcept;

    class Logging_Manager final
    {
    public:
        using Listener_Token = std::uint64_t;
        using Listener = std::function<void(const Log_Record&)>;

        class Scoped_Timer final
        {
        public:
            Scoped_Timer(
                Logging_Manager& manager,
                std::string channel,
                std::string name,
                std::source_location source) noexcept;
            ~Scoped_Timer();
            Scoped_Timer(Scoped_Timer&& other) noexcept;
            Scoped_Timer& operator=(Scoped_Timer&& other) noexcept;
            Scoped_Timer(const Scoped_Timer&) = delete;
            Scoped_Timer& operator=(const Scoped_Timer&) = delete;

        private:
            Logging_Manager* m_manager{};
            std::string m_channel;
            std::string m_name;
            std::chrono::steady_clock::time_point m_started{};
            std::source_location m_source{};
        };

        class Scoped_Context final
        {
        public:
            Scoped_Context(
                Logging_Manager& manager,
                std::initializer_list<Log_Field> fields);
            ~Scoped_Context();
            Scoped_Context(Scoped_Context&& other) noexcept;
            Scoped_Context& operator=(Scoped_Context&& other) noexcept;
            Scoped_Context(const Scoped_Context&) = delete;
            Scoped_Context& operator=(const Scoped_Context&) = delete;

        private:
            Logging_Manager* m_manager{};
            std::size_t m_previous_size{};
        };

        Logging_Manager() = default;
        ~Logging_Manager();
        Logging_Manager(const Logging_Manager&) = delete;
        Logging_Manager& operator=(const Logging_Manager&) = delete;

        bool initialize(Logging_Config config);
        void shutdown() noexcept;
        void flush();
        void rotate_now();

        void log(
            Log_Level level,
            std::string_view channel,
            std::string_view message,
            std::initializer_list<Log_Field> fields = {},
            std::source_location source = std::source_location::current());

        void trace(std::string_view channel, std::string_view message,
            std::initializer_list<Log_Field> fields = {},
            std::source_location source = std::source_location::current());
        void debug(std::string_view channel, std::string_view message,
            std::initializer_list<Log_Field> fields = {},
            std::source_location source = std::source_location::current());
        void info(std::string_view channel, std::string_view message,
            std::initializer_list<Log_Field> fields = {},
            std::source_location source = std::source_location::current());
        void warning(std::string_view channel, std::string_view message,
            std::initializer_list<Log_Field> fields = {},
            std::source_location source = std::source_location::current());
        void error(std::string_view channel, std::string_view message,
            std::initializer_list<Log_Field> fields = {},
            std::source_location source = std::source_location::current());
        void critical(std::string_view channel, std::string_view message,
            std::initializer_list<Log_Field> fields = {},
            std::source_location source = std::source_location::current());

        void log_exception(
            std::string_view channel,
            const std::exception& exception,
            std::string_view message = "Unhandled exception",
            std::source_location source = std::source_location::current());
        void hexdump(
            std::string_view channel,
            std::string_view label,
            std::span<const std::byte> data,
            std::size_t maximum_bytes = 256,
            std::source_location source = std::source_location::current());

        [[nodiscard]] Scoped_Timer scoped_timer(
            std::string_view channel,
            std::string_view name,
            std::source_location source = std::source_location::current());
        [[nodiscard]] Scoped_Context scoped_context(
            std::initializer_list<Log_Field> fields);

        void set_minimum_level(Log_Level level) noexcept;
        [[nodiscard]] Log_Level minimum_level() const noexcept;
        void set_channel_level(std::string channel, Log_Level level);
        void clear_channel_level(std::string_view channel);
        void clear_channel_levels();

        [[nodiscard]] Listener_Token add_listener(Listener listener);
        [[nodiscard]] bool remove_listener(Listener_Token token);
        [[nodiscard]] std::vector<Log_Record> recent_records(
            std::size_t maximum_records = 0) const;
        [[nodiscard]] Logging_Metrics metrics() const;
        [[nodiscard]] std::filesystem::path file_path() const;
        [[nodiscard]] bool initialized() const noexcept;

    private:
        [[nodiscard]] bool should_log(Log_Level level, std::string_view channel) const;
        void enqueue(Log_Record record);
        void worker_loop();
        void dispatch(const Log_Record& record);
        [[nodiscard]] std::string format_text(const Log_Record& record) const;
        [[nodiscard]] std::string format_json(const Log_Record& record) const;
        void rotate_if_required(std::size_t incoming_bytes);
        void rotate_files_unlocked();
        [[nodiscard]] std::size_t push_thread_context(
            std::initializer_list<Log_Field> fields);
        void restore_thread_context(std::size_t previous_size) noexcept;

        Logging_Config m_config;
        std::filesystem::path m_file_path;
        std::ofstream m_file;
        std::size_t m_current_file_size{};
        bool m_console_owned{};
        unsigned short m_console_default_attributes{7};

        std::atomic_bool m_initialized{false};
        std::atomic_bool m_accepting{false};
        std::atomic<Log_Level> m_minimum_level{Log_Level::info};
        std::atomic_uint64_t m_next_sequence{1};
        std::atomic_uint64_t m_next_listener_token{1};
        std::atomic_uint64_t m_pending_records{0};

        mutable std::mutex m_queue_mutex;
        std::condition_variable m_queue_condition;
        std::deque<Log_Record> m_queue;
        std::thread m_worker;

        mutable std::mutex m_output_mutex;
        mutable std::mutex m_recent_mutex;
        std::deque<Log_Record> m_recent_records;

        mutable std::mutex m_channel_mutex;
        std::unordered_map<std::string, Log_Level> m_channel_levels;

        mutable std::mutex m_listener_mutex;
        std::unordered_map<Listener_Token, Listener> m_listeners;

        mutable std::mutex m_flush_mutex;
        std::condition_variable m_flush_condition;

        std::atomic_uint64_t m_accepted_records{0};
        std::atomic_uint64_t m_written_records{0};
        std::atomic_uint64_t m_dropped_records{0};
        std::atomic_uint64_t m_rotated_files{0};
        std::atomic_uint64_t m_bytes_written{0};
        std::atomic_uint64_t m_listener_failures{0};
        std::atomic_size_t m_peak_queue_depth{0};
    };
}

#define RC_LOG_TRACE(manager, channel, message) \
    (manager).log(::reapercore::Log_Level::trace, (channel), (message), {}, std::source_location::current())
#define RC_LOG_DEBUG(manager, channel, message) \
    (manager).log(::reapercore::Log_Level::debug, (channel), (message), {}, std::source_location::current())
#define RC_LOG_INFO(manager, channel, message) \
    (manager).log(::reapercore::Log_Level::info, (channel), (message), {}, std::source_location::current())
#define RC_LOG_WARNING(manager, channel, message) \
    (manager).log(::reapercore::Log_Level::warning, (channel), (message), {}, std::source_location::current())
#define RC_LOG_ERROR(manager, channel, message) \
    (manager).log(::reapercore::Log_Level::error, (channel), (message), {}, std::source_location::current())
#define RC_LOG_CRITICAL(manager, channel, message) \
    (manager).log(::reapercore::Log_Level::critical, (channel), (message), {}, std::source_location::current())
#define RC_DETAIL_JOIN_INNER(left, right) left##right
#define RC_DETAIL_JOIN(left, right) RC_DETAIL_JOIN_INNER(left, right)
#define RC_LOG_SCOPE(manager, channel, name) \
    auto RC_DETAIL_JOIN(rc_log_scope_, __LINE__) = \
        (manager).scoped_timer((channel), (name), std::source_location::current())
