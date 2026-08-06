#pragma once

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
    };

    class D3D12_Backend final
    {
    public:
        bool initialize(Logging_Manager& logging) noexcept;
        bool attach_device(const D3D12_Device_Attach_Info& info) noexcept;
        void detach_device() noexcept;
        void begin_frame() noexcept;
        void end_frame() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] D3D12_Backend_State state() const noexcept;
        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] bool device_attached() const noexcept;
        [[nodiscard]] ID3D12Device* device() const noexcept;
        [[nodiscard]] D3D12_Srv_Descriptor_Allocator& srv_descriptors() noexcept;
        [[nodiscard]] const D3D12_Srv_Descriptor_Allocator& srv_descriptors() const noexcept;

    private:
        Logging_Manager* m_logging{};
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        D3D12_Srv_Descriptor_Allocator m_srv_descriptors;
        mutable std::mutex m_mutex;
        std::atomic<D3D12_Backend_State> m_state{D3D12_Backend_State::stopped};
        std::atomic_bool m_device_attached{false};
    };
}
