#pragma once

#include "reapercore/core/events/event.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace reapercore
{
    class Event_Bus final
    {
    public:
        using Subscription_Id = std::uint64_t;

        class Scoped_Subscription final
        {
        public:
            Scoped_Subscription() = default;
            Scoped_Subscription(Event_Bus& bus, Subscription_Id id) noexcept : m_bus(&bus), m_id(id) {}
            ~Scoped_Subscription() { reset(); }

            Scoped_Subscription(const Scoped_Subscription&) = delete;
            Scoped_Subscription& operator=(const Scoped_Subscription&) = delete;

            Scoped_Subscription(Scoped_Subscription&& other) noexcept
                : m_bus(std::exchange(other.m_bus, nullptr)), m_id(std::exchange(other.m_id, 0)) {}

            Scoped_Subscription& operator=(Scoped_Subscription&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    m_bus = std::exchange(other.m_bus, nullptr);
                    m_id = std::exchange(other.m_id, 0);
                }
                return *this;
            }

            void reset() noexcept
            {
                if (m_bus != nullptr && m_id != 0)
                    m_bus->unsubscribe(m_id);
                m_bus = nullptr;
                m_id = 0;
            }

            [[nodiscard]] bool valid() const noexcept { return m_bus != nullptr && m_id != 0; }

        private:
            Event_Bus* m_bus{};
            Subscription_Id m_id{};
        };

        template <typename Event_Type, typename Callback>
        [[nodiscard]] Scoped_Subscription subscribe(
            Callback&& callback,
            Event_Priority priority = Event_Priority::normal)
        {
            const auto id = m_next_id.fetch_add(1, std::memory_order_relaxed);

            Subscriber subscriber;
            subscriber.id = id;
            subscriber.priority = priority;
            subscriber.callback = [handler = std::forward<Callback>(callback)](const Event& event)
            {
                return std::invoke(handler, static_cast<const Event_Type&>(event));
            };

            std::scoped_lock lock(m_mutex);
            auto& bucket = m_subscribers[std::type_index(typeid(Event_Type))];
            bucket.push_back(std::move(subscriber));
            std::stable_sort(bucket.begin(), bucket.end(), [](const Subscriber& left, const Subscriber& right)
            {
                return static_cast<std::uint8_t>(left.priority) > static_cast<std::uint8_t>(right.priority);
            });

            return Scoped_Subscription(*this, id);
        }

        template <typename Event_Type>
        Event_Result publish(const Event_Type& event)
        {
            std::vector<Subscriber> subscribers;
            {
                std::scoped_lock lock(m_mutex);
                const auto iterator = m_subscribers.find(std::type_index(typeid(Event_Type)));
                if (iterator == m_subscribers.end())
                    return Event_Result::continue_dispatch;
                subscribers = iterator->second;
            }

            for (const auto& subscriber : subscribers)
            {
                const auto result = subscriber.callback(event);
                if (result == Event_Result::consume && subscriber.priority != Event_Priority::monitor)
                    return result;
            }

            return Event_Result::continue_dispatch;
        }

        bool unsubscribe(Subscription_Id id) noexcept
        {
            std::scoped_lock lock(m_mutex);
            for (auto& [_, bucket] : m_subscribers)
            {
                const auto previous_size = bucket.size();
                std::erase_if(bucket, [id](const Subscriber& subscriber)
                {
                    return subscriber.id == id;
                });
                if (bucket.size() != previous_size)
                    return true;
            }
            return false;
        }

        void clear() noexcept
        {
            std::scoped_lock lock(m_mutex);
            m_subscribers.clear();
        }

    private:
        struct Subscriber
        {
            Subscription_Id id{};
            Event_Priority priority{Event_Priority::normal};
            std::function<Event_Result(const Event&)> callback;
        };

        std::atomic_uint64_t m_next_id{1};
        std::mutex m_mutex;
        std::unordered_map<std::type_index, std::vector<Subscriber>> m_subscribers;
    };
}
