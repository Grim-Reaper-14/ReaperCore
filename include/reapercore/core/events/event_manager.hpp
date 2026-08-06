#pragma once

#include "reapercore/core/events/event_bus.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace reapercore
{
    struct Event_Metrics
    {
        std::uint64_t published{};
        std::uint64_t deferred{};
        std::uint64_t processed{};
        std::uint64_t dropped{};
        std::size_t queued{};
    };

    class Event_Manager final
    {
    public:
        explicit Event_Manager(std::size_t queue_capacity = 4096) noexcept;

        Event_Manager(const Event_Manager&) = delete;
        Event_Manager& operator=(const Event_Manager&) = delete;

        template <typename Event_Type, typename Callback>
        [[nodiscard]] Event_Bus::Scoped_Subscription subscribe(
            Callback&& callback,
            Event_Priority priority = Event_Priority::normal)
        {
            return m_bus.subscribe<Event_Type>(
                std::forward<Callback>(callback),
                priority);
        }

        template <typename Event_Type>
        Event_Result publish(const Event_Type& event)
        {
            m_published.fetch_add(1, std::memory_order_relaxed);
            return m_bus.publish(event);
        }

        template <typename Event_Type>
        bool defer(Event_Type event)
        {
            std::scoped_lock lock(m_queue_mutex);
            if (m_queue.size() >= m_queue_capacity)
            {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            m_queue.emplace_back([this, event = std::move(event)]() mutable
            {
                publish(event);
            });
            m_deferred.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        std::size_t process_deferred(std::size_t maximum_events = 0);
        void clear() noexcept;

        [[nodiscard]] Event_Metrics metrics() const noexcept;
        [[nodiscard]] Event_Bus& bus() noexcept;
        [[nodiscard]] const Event_Bus& bus() const noexcept;

    private:
        Event_Bus m_bus;
        const std::size_t m_queue_capacity;
        mutable std::mutex m_queue_mutex;
        std::deque<std::function<void()>> m_queue;
        std::atomic_uint64_t m_published{0};
        std::atomic_uint64_t m_deferred{0};
        std::atomic_uint64_t m_processed{0};
        std::atomic_uint64_t m_dropped{0};
    };
}
