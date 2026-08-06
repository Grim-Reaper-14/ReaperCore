#pragma once

#include "reapercore/backend/d3d12/execution_core.hpp"
#include "reapercore/backend/d3d12/srv_descriptor_allocator.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>

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

    struct D3D12_Device_Attach_Info
    {
        ID3D12Device* device{};
        std::uint32_t srv_descriptor_capacity{64};
        std::uint32_t frame_count{3};
    };

    class D3D12_Backend final
    {
    public:
        bool initialize(Logging_Manager& logging) noexcept;
        bool attach_device(const D3D12_Device_Attach_Info& info) noexcept;
        void detach_device() noexcept;
        bool begin_frame() noexcept;
        bool end_frame() noexcept;
        bool flush(std::uint32_t timeout_ms = INFINITE) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] D3D12_Backend_State state() const noexcept;
        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] bool device_attached() const noexcept;
        [[nodiscard]] ID3D12Device* device() const noexcept;
        [[nodiscard]] ID3D12GraphicsCommandList* command_list() const noexcept;
        [[nodiscard]] D3D12_Command_Queue_Manager& command_queue() noexcept;
        [[nodiscard]] D3D12_Fence_Manager& fence() noexcept;
        [[nodiscard]] D3D12_Frame_Resource_Manager& frame_resources() noexcept;
        [[nodiscard]] D3D12_Srv_Descriptor_Allocator& srv_descriptors() noexcept;
        [[nodiscard]] const D3D12_Srv_Descriptor_Allocator& srv_descriptors() const noexcept;

    private:
        Logging_Manager* m_logging{};
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        D3D12_Command_Queue_Manager m_command_queue;
        D3D12_Fence_Manager m_fence;
        D3D12_Frame_Resource_Manager m_frame_resources;
        D3D12_Srv_Descriptor_Allocator m_srv_descriptors;
        mutable std::mutex m_mutex;
        std::atomic<D3D12_Backend_State> m_state{D3D12_Backend_State::stopped};
        std::atomic_bool m_device_attached{false};
        std::atomic_bool m_frame_open{false};
    };
}
