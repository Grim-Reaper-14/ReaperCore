#pragma once

#include "reapercore/core/tasks/task.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace reapercore
{
    class Event_Manager;
    class Logging_Manager;

    class Task_Manager final
    {
    public:
        Task_Manager() = default;
        ~Task_Manager();

        Task_Manager(const Task_Manager&) = delete;
        Task_Manager& operator=(const Task_Manager&) = delete;

        bool initialize(
            Logging_Manager& logging,
            Event_Manager& events,
            std::size_t worker_count = 0,
            std::size_t queue_capacity = 8192);
        void shutdown() noexcept;

        [[nodiscard]] Task_Id submit(
            std::string name,
            Task_Function function,
            Task_Priority priority = Task_Priority::normal,
            Cancellation_Token cancellation = {});

        [[nodiscard]] Task_Id submit_main_thread(
            std::string name,
            Task_Function function,
            Task_Priority priority = Task_Priority::normal,
            Cancellation_Token cancellation = {});

        [[nodiscard]] Task_Id schedule_after(
            std::chrono::milliseconds delay,
            std::string name,
            Task_Function function,
            Task_Priority priority = Task_Priority::normal,
            Task_Queue queue = Task_Queue::worker,
            Cancellation_Token cancellation = {});

        std::size_t process_main_thread(std::size_t maximum_tasks = 0);
        bool cancel(Task_Id id) noexcept;
        void cancel_all() noexcept;

        [[nodiscard]] Task_Metrics metrics() const noexcept;
        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] std::size_t worker_count() const noexcept;

    private:
        struct Queued_Task
        {
            Task_Descriptor descriptor;
            std::shared_ptr<std::atomic_bool> cancelled;
        };

        static bool higher_priority(const Queued_Task& left, const Queued_Task& right) noexcept;
        void worker_loop(std::size_t worker_index);
        std::optional<Queued_Task> take_ready_main_thread_task();
        Task_Id enqueue(Task_Descriptor descriptor);
        void execute(Queued_Task& task, bool worker_thread) noexcept;
        void sort_queue(std::deque<Queued_Task>& queue);
        void erase_tracking(Task_Id id) noexcept;

        Logging_Manager* m_logging{};
        Event_Manager* m_events{};
        std::size_t m_queue_capacity{8192};

        mutable std::mutex m_mutex;
        std::condition_variable m_condition;
        std::deque<Queued_Task> m_worker_queue;
        std::deque<Queued_Task> m_main_thread_queue;
        std::unordered_map<Task_Id, std::weak_ptr<std::atomic_bool>> m_cancellations;
        std::vector<std::thread> m_workers;

        std::atomic_bool m_running{false};
        std::atomic_uint64_t m_next_id{1};
        std::atomic_uint64_t m_submitted{0};
        std::atomic_uint64_t m_completed{0};
        std::atomic_uint64_t m_cancelled{0};
        std::atomic_uint64_t m_failed{0};
        std::atomic_uint64_t m_active_workers{0};
    };
}
