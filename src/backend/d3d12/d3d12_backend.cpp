#include "reapercore/backend/d3d12/d3d12_backend.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <string>

namespace reapercore
{
    bool D3D12_Backend::initialize(Logging_Manager& logging) noexcept
    {
        std::scoped_lock lock(m_mutex);

        const auto current = m_state.load(std::memory_order_acquire);
        if (current == D3D12_Backend_State::initialized)
            return true;

        m_logging = &logging;
        m_state.store(D3D12_Backend_State::available, std::memory_order_release);
        m_logging->info("d3d12", "D3D12 backend interface is available.");

        m_state.store(D3D12_Backend_State::initialized, std::memory_order_release);
        m_logging->info("d3d12", "D3D12 backend lifecycle initialized.");
        return true;
    }

    bool D3D12_Backend::attach_device(
        const D3D12_Device_Attach_Info& info) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!ready() || info.device == nullptr ||
            info.srv_descriptor_capacity == 0)
        {
            if (m_logging != nullptr)
                m_logging->error("d3d12", "Invalid DX12 device attachment data.");
            return false;
        }

        if (m_device_attached.load(std::memory_order_acquire))
        {
            if (m_device.Get() == info.device)
                return true;

            if (m_logging != nullptr)
                m_logging->error("d3d12", "A different DX12 device is already attached.");
            return false;
        }

        m_device = info.device;
        if (!m_srv_descriptors.initialize(
                *m_logging,
                *m_device.Get(),
                info.srv_descriptor_capacity))
        {
            m_device.Reset();
            return false;
        }

        m_device_attached.store(true, std::memory_order_release);
        if (m_logging != nullptr)
        {
            m_logging->info("d3d12", "DX12 device attached.", {
                {"srv_descriptor_capacity", std::to_string(info.srv_descriptor_capacity)}
            });
        }
        return true;
    }

    void D3D12_Backend::detach_device() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_device_attached.exchange(false, std::memory_order_acq_rel))
            return;

        m_srv_descriptors.shutdown();
        m_device.Reset();

        if (m_logging != nullptr)
            m_logging->info("d3d12", "DX12 device detached.");
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
        detach_device();

        std::scoped_lock lock(m_mutex);
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

    bool D3D12_Backend::device_attached() const noexcept
    {
        return m_device_attached.load(std::memory_order_acquire);
    }

    ID3D12Device* D3D12_Backend::device() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_device.Get();
    }

    D3D12_Srv_Descriptor_Allocator& D3D12_Backend::srv_descriptors() noexcept
    {
        return m_srv_descriptors;
    }

    const D3D12_Srv_Descriptor_Allocator& D3D12_Backend::srv_descriptors() const noexcept
    {
        return m_srv_descriptors;
    }
}
