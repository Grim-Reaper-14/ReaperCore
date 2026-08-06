#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace reapercore
{
    class Task_Manager;

    class Cancellation_Token final
    {
    public:
        Cancellation_Token() = default;

        [[nodiscard]] bool cancellation_requested() const noexcept
        {
            const bool primary_requested =
                m_primary_state != nullptr &&
                m_primary_state->load(std::memory_order_acquire);
            const bool secondary_requested =
                m_secondary_state != nullptr &&
                m_secondary_state->load(std::memory_order_acquire);
            return primary_requested || secondary_requested;
        }

        explicit operator bool() const noexcept
        {
            return m_primary_state != nullptr || m_secondary_state != nullptr;
        }

    private:
        explicit Cancellation_Token(
            std::shared_ptr<std::atomic_bool> primary_state,
            std::shared_ptr<std::atomic_bool> secondary_state = {}) noexcept :
            m_primary_state(std::move(primary_state)),
            m_secondary_state(std::move(secondary_state))
        {
        }

        std::shared_ptr<std::atomic_bool> m_primary_state;
        std::shared_ptr<std::atomic_bool> m_secondary_state;

        friend class Cancellation_Source;
        friend class Task_Manager;
    };

    class Cancellation_Source final
    {
    public:
        Cancellation_Source() :
            m_state(std::make_shared<std::atomic_bool>(false))
        {
        }

        [[nodiscard]] Cancellation_Token token() const noexcept
        {
            return Cancellation_Token(m_state);
        }

        void request_cancellation() noexcept
        {
            m_state->store(true, std::memory_order_release);
        }

        [[nodiscard]] bool cancellation_requested() const noexcept
        {
            return m_state->load(std::memory_order_acquire);
        }

    private:
        std::shared_ptr<std::atomic_bool> m_state;
    };
}
