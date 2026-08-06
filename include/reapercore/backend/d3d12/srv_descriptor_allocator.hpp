#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace reapercore
{
    class Logging_Manager;

    struct D3D12_Srv_Allocator_Metrics
    {
        std::uint32_t capacity{};
        std::uint32_t allocated{};
        std::uint32_t available{};
        std::uint32_t peak_allocated{};
        std::uint64_t allocation_failures{};
    };

    class D3D12_Srv_Descriptor_Allocator final
    {
    public:
        D3D12_Srv_Descriptor_Allocator() = default;
        ~D3D12_Srv_Descriptor_Allocator();

        D3D12_Srv_Descriptor_Allocator(
            const D3D12_Srv_Descriptor_Allocator&) = delete;
        D3D12_Srv_Descriptor_Allocator& operator=(
            const D3D12_Srv_Descriptor_Allocator&) = delete;

        bool initialize(
            Logging_Manager& logging,
            ID3D12Device& device,
            std::uint32_t capacity = 64) noexcept;
        void shutdown() noexcept;

        bool allocate(
            D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle,
            D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle) noexcept;
        bool release(
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) noexcept;

        static void imgui_allocate(
            void* user_data,
            D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle,
            D3D12_GPU_DESCRIPTOR_HANDLE* gpu_handle) noexcept;
        static void imgui_release(
            void* user_data,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] ID3D12DescriptorHeap* heap() const noexcept;
        [[nodiscard]] std::uint32_t capacity() const noexcept;
        [[nodiscard]] D3D12_Srv_Allocator_Metrics metrics() const noexcept;

    private:
        [[nodiscard]] bool resolve_index(
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
            std::uint32_t& index) const noexcept;

        Logging_Manager* m_logging{};
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;

        mutable std::mutex m_mutex;
        std::vector<std::uint32_t> m_free_indices;
        std::vector<std::uint8_t> m_allocated_slots;
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpu_start{};
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpu_start{};
        std::uint32_t m_descriptor_size{};
        std::uint32_t m_capacity{};
        std::uint32_t m_allocated_count{};
        std::uint32_t m_peak_allocated{};
        std::uint64_t m_allocation_failures{};
        std::atomic_bool m_initialized{false};
    };
}
