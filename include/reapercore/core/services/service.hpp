#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace reapercore
{
    enum class Service_State : std::uint8_t
    {
        registered,
        initializing,
        initialized,
        starting,
        running,
        stopping,
        stopped,
        failed
    };

    class Service
    {
    public:
        virtual ~Service() = default;

        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
        [[nodiscard]] virtual std::vector<std::string_view> dependencies() const
        {
            return {};
        }

        virtual bool initialize() = 0;
        virtual bool start()
        {
            return true;
        }
        virtual void tick()
        {
        }
        virtual void stop() noexcept
        {
        }
        virtual void shutdown() noexcept = 0;
    };
}
