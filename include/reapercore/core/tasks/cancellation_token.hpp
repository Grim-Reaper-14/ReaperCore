#pragma once

#include <atomic>
#include <memory>

namespace reapercore
{
    class Cancellation_Token final
    {
    public:
        Cancellation_Token() = default;

        [[nodiscard]] bool cancellation_requested() const noexcept
        {
            return m_state != nullptr && m_state->load(std::memory_order_acquire);
        }

        explicit operator bool() const noexcept
        {
            return m_state != nullptr;
        }

    private:
        explicit Cancellation_Token(std::shared_ptr<std::atomic_bool> state) noexcept :
            m_state(std::move(state))
        {
        }

        std::shared_ptr<std::atomic_bool> m_state;

        friend class Cancellation_Source;
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
