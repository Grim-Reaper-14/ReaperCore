#pragma once

#include "reapercore/core/tasks/cancellation_token.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace reapercore
{
    enum class Task_Priority : std::uint8_t
    {
        low,
        normal,
        high,
        critical
    };

    enum class Task_Queue : std::uint8_t
    {
        worker,
        main_thread
    };

    using Task_Id = std::uint64_t;
    using Task_Function = std::function<void(const Cancellation_Token&)>;

    struct Task_Descriptor
    {
        Task_Id id{};
        std::string name;
        Task_Priority priority{Task_Priority::normal};
        Task_Queue queue{Task_Queue::worker};
        std::chrono::steady_clock::time_point ready_at{std::chrono::steady_clock::now()};
        Cancellation_Token cancellation;
        Task_Function function;
    };

    struct Task_Metrics
    {
        std::uint64_t submitted{};
        std::uint64_t completed{};
        std::uint64_t cancelled{};
        std::uint64_t failed{};
        std::uint64_t queued_worker{};
        std::uint64_t queued_main_thread{};
        std::uint64_t active_workers{};
    };
}
