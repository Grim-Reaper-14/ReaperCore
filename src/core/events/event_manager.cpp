#include "reapercore/core/events/event_manager.hpp"

#include <algorithm>
#include <utility>

namespace reapercore
{
    Event_Manager::Event_Manager(const std::size_t queue_capacity) noexcept :
        m_queue_capacity(std::max<std::size_t>(queue_capacity, 1))
    {
    }

    std::size_t Event_Manager::process_deferred(const std::size_t maximum_events)
    {
        std::deque<std::function<void()>> pending;
        {
            std::scoped_lock lock(m_queue_mutex);
            const auto count = maximum_events == 0
                ? m_queue.size()
                : std::min(maximum_events, m_queue.size());

            for (std::size_t index = 0; index < count; ++index)
            {
                pending.push_back(std::move(m_queue.front()));
                m_queue.pop_front();
            }
        }

        std::size_t processed{};
        for (auto& callback : pending)
        {
            callback();
            ++processed;
        }

        m_processed.fetch_add(processed, std::memory_order_relaxed);
        return processed;
    }

    void Event_Manager::clear() noexcept
    {
        {
            std::scoped_lock lock(m_queue_mutex);
            m_queue.clear();
        }
        m_bus.clear();
    }

    Event_Metrics Event_Manager::metrics() const noexcept
    {
        std::size_t queued{};
        {
            std::scoped_lock lock(m_queue_mutex);
            queued = m_queue.size();
        }

        return {
            m_published.load(std::memory_order_relaxed),
            m_deferred.load(std::memory_order_relaxed),
            m_processed.load(std::memory_order_relaxed),
            m_dropped.load(std::memory_order_relaxed),
            queued
        };
    }

    Event_Bus& Event_Manager::bus() noexcept
    {
        return m_bus;
    }

    const Event_Bus& Event_Manager::bus() const noexcept
    {
        return m_bus;
    }
}
