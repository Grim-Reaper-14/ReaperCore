#include "reapercore/core/tasks/task_manager.hpp"
#include "reapercore/core/events/event_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace reapercore
{
    Task_Manager::~Task_Manager()
    {
        shutdown();
    }

    bool Task_Manager::initialize(
        Logging_Manager& logging,
        Event_Manager& events,
        std::size_t worker_count_value,
        const std::size_t queue_capacity)
    {
        if (m_running.exchange(true, std::memory_order_acq_rel))
            return true;

        m_logging = &logging;
        m_events = &events;
        m_queue_capacity = std::max<std::size_t>(queue_capacity, 1);

        if (worker_count_value == 0)
        {
            const auto hardware_threads = std::thread::hardware_concurrency();
            worker_count_value = hardware_threads > 1 ? hardware_threads - 1 : 1;
        }

        try
        {
            m_workers.reserve(worker_count_value);
            for (std::size_t index = 0; index < worker_count_value; ++index)
                m_workers.emplace_back(&Task_Manager::worker_loop, this, index);
        }
        catch (...)
        {
            m_running.store(false, std::memory_order_release);
            m_condition.notify_all();
            for (auto& worker : m_workers)
            {
                if (worker.joinable())
                    worker.join();
            }
            m_workers.clear();
            m_logging = nullptr;
            m_events = nullptr;
            return false;
        }

        m_logging->info("tasks", "Task manager initialized.", {
            {"workers", std::to_string(m_workers.size())},
            {"queue_capacity", std::to_string(m_queue_capacity)}
        });
        return true;
    }

    void Task_Manager::shutdown() noexcept
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel))
            return;

        cancel_all();
        m_condition.notify_all();

        for (auto& worker : m_workers)
        {
            if (worker.joinable())
                worker.join();
        }
        m_workers.clear();

        {
            std::scoped_lock lock(m_mutex);
            m_worker_queue.clear();
            m_main_thread_queue.clear();
            m_cancellations.clear();
        }

        if (m_logging != nullptr)
        {
            const auto snapshot = metrics();
            m_logging->info("tasks", "Task manager stopped.", {
                {"submitted", std::to_string(snapshot.submitted)},
                {"completed", std::to_string(snapshot.completed)},
                {"cancelled", std::to_string(snapshot.cancelled)},
                {"failed", std::to_string(snapshot.failed)}
            });
        }

        m_events = nullptr;
        m_logging = nullptr;
    }

    Task_Id Task_Manager::submit(
        std::string name,
        Task_Function function,
        const Task_Priority priority,
        Cancellation_Token cancellation)
    {
        Task_Descriptor descriptor;
        descriptor.name = std::move(name);
        descriptor.priority = priority;
        descriptor.queue = Task_Queue::worker;
        descriptor.ready_at = std::chrono::steady_clock::time_point::min();
        descriptor.cancellation = std::move(cancellation);
        descriptor.function = std::move(function);
        return enqueue(std::move(descriptor));
    }

    Task_Id Task_Manager::submit_main_thread(
        std::string name,
        Task_Function function,
        const Task_Priority priority,
        Cancellation_Token cancellation)
    {
        Task_Descriptor descriptor;
        descriptor.name = std::move(name);
        descriptor.priority = priority;
        descriptor.queue = Task_Queue::main_thread;
        descriptor.ready_at = std::chrono::steady_clock::time_point::min();
        descriptor.cancellation = std::move(cancellation);
        descriptor.function = std::move(function);
        return enqueue(std::move(descriptor));
    }

    Task_Id Task_Manager::schedule_after(
        const std::chrono::milliseconds delay,
        std::string name,
        Task_Function function,
        const Task_Priority priority,
        const Task_Queue queue,
        Cancellation_Token cancellation)
    {
        Task_Descriptor descriptor;
        descriptor.name = std::move(name);
        descriptor.priority = priority;
        descriptor.queue = queue;
        descriptor.ready_at = delay <= std::chrono::milliseconds::zero()
            ? std::chrono::steady_clock::time_point::min()
            : std::chrono::steady_clock::now() + delay;
        descriptor.cancellation = std::move(cancellation);
        descriptor.function = std::move(function);
        return enqueue(std::move(descriptor));
    }

    std::size_t Task_Manager::process_main_thread(const std::size_t maximum_tasks)
    {
        const auto limit = maximum_tasks == 0
            ? std::numeric_limits<std::size_t>::max()
            : maximum_tasks;
        std::size_t processed{};

        while (processed < limit)
        {
            auto task = take_ready_main_thread_task();
            if (!task.has_value())
                break;

            execute(*task, false);
            ++processed;
        }

        return processed;
    }

    bool Task_Manager::cancel(const Task_Id id) noexcept
    {
        std::scoped_lock lock(m_mutex);
        const auto iterator = m_cancellations.find(id);
        if (iterator == m_cancellations.end())
            return false;

        if (const auto state = iterator->second.lock())
        {
            const bool was_cancelled = state->exchange(true, std::memory_order_acq_rel);
            if (!was_cancelled)
                m_cancelled.fetch_add(1, std::memory_order_relaxed);
            m_condition.notify_all();
            return !was_cancelled;
        }

        m_cancellations.erase(iterator);
        return false;
    }

    void Task_Manager::cancel_all() noexcept
    {
        std::scoped_lock lock(m_mutex);
        for (auto& [_, weak_state] : m_cancellations)
        {
            if (const auto state = weak_state.lock())
            {
                if (!state->exchange(true, std::memory_order_acq_rel))
                    m_cancelled.fetch_add(1, std::memory_order_relaxed);
            }
        }
        m_condition.notify_all();
    }

    Task_Metrics Task_Manager::metrics() const noexcept
    {
        std::size_t worker_queued{};
        std::size_t main_queued{};
        {
            std::scoped_lock lock(m_mutex);
            worker_queued = m_worker_queue.size();
            main_queued = m_main_thread_queue.size();
        }

        return {
            m_submitted.load(std::memory_order_relaxed),
            m_completed.load(std::memory_order_relaxed),
            m_cancelled.load(std::memory_order_relaxed),
            m_failed.load(std::memory_order_relaxed),
            worker_queued,
            main_queued,
            m_active_workers.load(std::memory_order_relaxed)
        };
    }

    bool Task_Manager::running() const noexcept
    {
        return m_running.load(std::memory_order_acquire);
    }

    std::size_t Task_Manager::worker_count() const noexcept
    {
        return m_workers.size();
    }

    bool Task_Manager::higher_priority(
        const Queued_Task& left,
        const Queued_Task& right) noexcept
    {
        if (left.descriptor.ready_at != right.descriptor.ready_at)
            return left.descriptor.ready_at < right.descriptor.ready_at;
        return static_cast<std::uint8_t>(left.descriptor.priority) >
            static_cast<std::uint8_t>(right.descriptor.priority);
    }

    void Task_Manager::worker_loop(const std::size_t worker_index)
    {
        if (m_logging != nullptr)
        {
            m_logging->debug("tasks", "Worker started.", {
                {"worker", std::to_string(worker_index)}
            });
        }

        while (m_running.load(std::memory_order_acquire))
        {
            std::optional<Queued_Task> task;
            {
                std::unique_lock lock(m_mutex);
                m_condition.wait(lock, [this]
                {
                    return !m_running.load(std::memory_order_acquire) ||
                        !m_worker_queue.empty();
                });

                if (!m_running.load(std::memory_order_acquire))
                    break;

                if (!m_worker_queue.empty())
                {
                    const auto now = std::chrono::steady_clock::now();
                    const auto ready_at = m_worker_queue.front().descriptor.ready_at;
                    if (ready_at > now)
                    {
                        m_condition.wait_until(lock, ready_at);
                        continue;
                    }

                    task.emplace(std::move(m_worker_queue.front()));
                    m_worker_queue.pop_front();
                }
            }

            if (task.has_value())
                execute(*task, true);
        }
    }

    std::optional<Task_Manager::Queued_Task>
    Task_Manager::take_ready_main_thread_task()
    {
        std::scoped_lock lock(m_mutex);
        if (m_main_thread_queue.empty() ||
            m_main_thread_queue.front().descriptor.ready_at >
                std::chrono::steady_clock::now())
        {
            return std::nullopt;
        }

        Queued_Task task = std::move(m_main_thread_queue.front());
        m_main_thread_queue.pop_front();
        return task;
    }

    Task_Id Task_Manager::enqueue(Task_Descriptor descriptor)
    {
        if (!m_running.load(std::memory_order_acquire) || !descriptor.function)
            return 0;

        descriptor.id = m_next_id.fetch_add(1, std::memory_order_relaxed);
        if (descriptor.name.empty())
            descriptor.name = "unnamed-task";

        auto cancellation_state = std::make_shared<std::atomic_bool>(false);
        Queued_Task queued{std::move(descriptor), cancellation_state};

        std::scoped_lock lock(m_mutex);
        const auto total_queued =
            m_worker_queue.size() + m_main_thread_queue.size();
        if (total_queued >= m_queue_capacity)
        {
            if (m_logging != nullptr)
                m_logging->error("tasks", "Task queue capacity reached.");
            return 0;
        }

        const Task_Id id = queued.descriptor.id;
        m_cancellations[id] = cancellation_state;

        auto& queue = queued.descriptor.queue == Task_Queue::worker
            ? m_worker_queue
            : m_main_thread_queue;
        queue.push_back(std::move(queued));
        sort_queue(queue);
        m_submitted.fetch_add(1, std::memory_order_relaxed);
        m_condition.notify_one();
        return id;
    }

    void Task_Manager::execute(Queued_Task& task, const bool worker_thread) noexcept
    {
        Cancellation_Token effective_cancellation = task.descriptor.cancellation;
        effective_cancellation.m_states.emplace_back(task.cancelled);

        if (effective_cancellation.cancellation_requested())
        {
            if (!task.cancelled->load(std::memory_order_acquire))
                m_cancelled.fetch_add(1, std::memory_order_relaxed);
            erase_tracking(task.descriptor.id);
            return;
        }

        if (worker_thread)
            m_active_workers.fetch_add(1, std::memory_order_relaxed);

        try
        {
            task.descriptor.function(effective_cancellation);
            m_completed.fetch_add(1, std::memory_order_relaxed);
        }
        catch (const std::exception& exception)
        {
            m_failed.fetch_add(1, std::memory_order_relaxed);
            if (m_logging != nullptr)
                m_logging->log_exception("tasks", exception, "Task execution failed.");
        }
        catch (...)
        {
            m_failed.fetch_add(1, std::memory_order_relaxed);
            if (m_logging != nullptr)
            {
                m_logging->error(
                    "tasks",
                    "Task execution failed with an unknown exception.",
                    {{"task", task.descriptor.name}});
            }
        }

        if (worker_thread)
            m_active_workers.fetch_sub(1, std::memory_order_relaxed);
        erase_tracking(task.descriptor.id);
    }

    void Task_Manager::sort_queue(std::deque<Queued_Task>& queue)
    {
        std::stable_sort(queue.begin(), queue.end(), higher_priority);
    }

    void Task_Manager::erase_tracking(const Task_Id id) noexcept
    {
        std::scoped_lock lock(m_mutex);
        m_cancellations.erase(id);
    }
}
