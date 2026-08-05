#include "reapercore/backend/d3d12/d3d12_backend.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

namespace reapercore
{
    bool D3D12_Backend::initialize(Logging_Manager& logging) noexcept
    {
        m_logging = &logging;

        const auto current = m_state.load(std::memory_order_acquire);
        if (current == D3D12_Backend_State::initialized)
            return true;

        m_state.store(D3D12_Backend_State::available, std::memory_order_release);
        m_logging->info("d3d12", "D3D12 backend interface is available.");

        // Device, queue, swap-chain, descriptor heaps, and frame resources
        // will be attached by the platform-specific renderer integration.
        m_state.store(D3D12_Backend_State::initialized, std::memory_order_release);
        m_logging->info("d3d12", "D3D12 backend lifecycle initialized.");
        return true;
    }

    void D3D12_Backend::begin_frame() noexcept
    {
        if (!ready())
            return;
    }

    void D3D12_Backend::end_frame() noexcept
    {
        if (!ready())
            return;
    }

    void D3D12_Backend::shutdown() noexcept
    {
        const auto previous = m_state.exchange(
            D3D12_Backend_State::stopped,
            std::memory_order_acq_rel);

        if (previous != D3D12_Backend_State::stopped && m_logging != nullptr)
            m_logging->info("d3d12", "D3D12 backend stopped.");

        m_logging = nullptr;
    }

    D3D12_Backend_State D3D12_Backend::state() const noexcept
    {
        return m_state.load(std::memory_order_acquire);
    }

    bool D3D12_Backend::ready() const noexcept
    {
        return state() == D3D12_Backend_State::initialized;
    }
}
