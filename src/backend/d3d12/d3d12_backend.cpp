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
            info.srv_descriptor_capacity == 0 || info.frame_count == 0)
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

        // GTA owns the graphics queue and the Present renderer owns the overlay
        // allocators, command list, and fence. The high-level attachment only
        // needs the game's device plus a shader-visible SRV heap for ImGui and
        // frontend textures. Creating a second execution stack here is both
        // redundant and unsafe inside the injected render path.
        if (!m_srv_descriptors.initialize(
                *m_logging,
                *m_device.Get(),
                info.srv_descriptor_capacity))
        {
            m_srv_descriptors.shutdown();
            m_device.Reset();
            m_state.store(D3D12_Backend_State::failed, std::memory_order_release);
            return false;
        }

        m_frame_open.store(false, std::memory_order_release);
        m_device_attached.store(true, std::memory_order_release);
        m_logging->info("d3d12", "DX12 external device attached for descriptor services.", {
            {"srv_descriptor_capacity", std::to_string(info.srv_descriptor_capacity)}
        });
        return true;
    }

    void D3D12_Backend::detach_device() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_device_attached.exchange(false, std::memory_order_acq_rel))
            return;

        if (m_frame_open.load(std::memory_order_acquire))
        {
            static_cast<void>(m_frame_resources.close_frame());
            m_frame_open.store(false, std::memory_order_release);
        }

        if (auto* queue = m_command_queue.queue(); queue != nullptr)
            static_cast<void>(m_fence.flush(*queue));

        m_srv_descriptors.shutdown();
        m_frame_resources.shutdown();
        m_fence.shutdown();
        m_command_queue.shutdown();
        m_device.Reset();

        if (m_logging != nullptr)
            m_logging->info("d3d12", "DX12 device detached.");
    }

    bool D3D12_Backend::begin_frame() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!ready() || !device_attached() ||
            m_frame_open.load(std::memory_order_acquire))
        {
            return false;
        }

        if (!m_frame_resources.begin_frame(m_fence))
            return false;

        m_frame_open.store(true, std::memory_order_release);
        return true;
    }

    bool D3D12_Backend::end_frame() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!ready() || !device_attached() ||
            !m_frame_open.load(std::memory_order_acquire))
        {
            return false;
        }

        if (!m_frame_resources.close_frame())
            return false;

        ID3D12CommandList* lists[]{m_frame_resources.command_list()};
        if (lists[0] == nullptr || !m_command_queue.execute(lists, 1))
            return false;

        auto* queue = m_command_queue.queue();
        if (queue == nullptr)
            return false;

        const auto fence_value = m_fence.signal(*queue);
        if (fence_value == 0 || !m_frame_resources.mark_submitted(fence_value))
            return false;

        m_frame_open.store(false, std::memory_order_release);
        return true;
    }

    bool D3D12_Backend::flush(const std::uint32_t timeout_ms) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!device_attached())
            return false;

        auto* queue = m_command_queue.queue();
        return queue != nullptr && m_fence.flush(*queue, timeout_ms);
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

    ID3D12GraphicsCommandList* D3D12_Backend::command_list() const noexcept
    {
        return m_frame_resources.command_list();
    }

    D3D12_Command_Queue_Manager& D3D12_Backend::command_queue() noexcept
    {
        return m_command_queue;
    }

    D3D12_Fence_Manager& D3D12_Backend::fence() noexcept
    {
        return m_fence;
    }

    D3D12_Frame_Resource_Manager& D3D12_Backend::frame_resources() noexcept
    {
        return m_frame_resources;
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
