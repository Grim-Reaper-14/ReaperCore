#pragma once

#include <atomic>

namespace reapercore
{
    class Logging_Manager;

    enum class D3D12_Backend_State
    {
        stopped,
        available,
        initialized,
        failed
    };

    class D3D12_Backend final
    {
    public:
        bool initialize(Logging_Manager& logging) noexcept;
        void begin_frame() noexcept;
        void end_frame() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] D3D12_Backend_State state() const noexcept;
        [[nodiscard]] bool ready() const noexcept;

    private:
        Logging_Manager* m_logging{};
        std::atomic<D3D12_Backend_State> m_state{D3D12_Backend_State::stopped};
    };
}
