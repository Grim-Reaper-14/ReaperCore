#pragma once

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace reapercore
{
    class Task_Manager;

    class Cancellation_Token final
    {
    public:
        Cancellation_Token() = default;

        [[nodiscard]] bool cancellation_requested() const noexcept
        {
            for (const auto& state : m_states)
            {
                if (state != nullptr && state->load(std::memory_order_acquire))
                    return true;
            }
            return false;
        }

        explicit operator bool() const noexcept
        {
            return !m_states.empty();
        }

    private:
        explicit Cancellation_Token(std::shared_ptr<std::atomic_bool> state)
        {
            if (state != nullptr)
                m_states.emplace_back(std::move(state));
        }

        std::vector<std::shared_ptr<std::atomic_bool>> m_states;

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

        [[nodiscard]] Cancellation_Token token() const
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
