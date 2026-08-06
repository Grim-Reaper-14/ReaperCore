#include "reapercore/backend/d3d12/srv_descriptor_allocator.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace reapercore
{
    D3D12_Srv_Descriptor_Allocator::~D3D12_Srv_Descriptor_Allocator()
    {
        shutdown();
    }

    bool D3D12_Srv_Descriptor_Allocator::initialize(
        Logging_Manager& logging,
        ID3D12Device& device,
        const std::uint32_t capacity_value) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        if (capacity_value == 0)
        {
            logging.error("d3d12", "SRV descriptor allocator capacity must be greater than zero.");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.NumDescriptors = capacity_value;
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        description.NodeMask = 0;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        const HRESULT result = device.CreateDescriptorHeap(
            &description,
            IID_PPV_ARGS(heap.GetAddressOf()));
        if (FAILED(result))
        {
            logging.error("d3d12", "Failed to create shader-visible SRV descriptor heap.", {
                {"hresult", std::to_string(static_cast<long long>(result))},
                {"capacity", std::to_string(capacity_value)}
            });
            return false;
        }

        const auto descriptor_size = device.GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (descriptor_size == 0)
        {
            logging.error("d3d12", "DX12 returned an invalid SRV descriptor size.");
            return false;
        }

        std::vector<std::uint8_t> allocated_slots;
        std::vector<std::uint32_t> free_indices;
        try
        {
            allocated_slots.assign(capacity_value, 0);
            free_indices.reserve(capacity_value);
            for (std::uint32_t index = capacity_value; index > 0; --index)
                free_indices.push_back(index - 1);
        }
        catch (...)
        {
            logging.error("d3d12", "Failed to allocate SRV descriptor allocator bookkeeping.");
            return false;
        }

        m_logging = &logging;
        m_device = &device;
        m_heap = std::move(heap);
        m_cpu_start = m_heap->GetCPUDescriptorHandleForHeapStart();
        m_gpu_start = m_heap->GetGPUDescriptorHandleForHeapStart();
        m_descriptor_size = descriptor_size;
        m_capacity = capacity_value;
        m_allocated_count = 0;
        m_peak_allocated = 0;
        m_allocation_failures = 0;
        m_allocated_slots = std::move(allocated_slots);
        m_free_indices = std::move(free_indices);

        m_initialized.store(true, std::memory_order_release);
        m_logging->info("d3d12", "SRV descriptor allocator initialized.", {
            {"capacity", std::to_string(m_capacity)},
            {"descriptor_size", std::to_string(m_descriptor_size)}
        });
        return true;
    }

    void D3D12_Srv_Descriptor_Allocator::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.exchange(false, std::memory_order_acq_rel))
            return;

        if (m_logging != nullptr)
        {
            m_logging->info("d3d12", "SRV descriptor allocator stopped.", {
                {"capacity", std::to_string(m_capacity)},
                {"peak_allocated", std::to_string(m_peak_allocated)},
                {"allocation_failures", std::to_string(m_allocation_failures)}
            });
        }

        m_free_indices.clear();
        m_allocated_slots.clear();
        m_heap.Reset();
        m_device.Reset();
        m_cpu_start = {};
        m_gpu_start = {};
        m_descriptor_size = 0;
        m_capacity = 0;
        m_allocated_count = 0;
        m_peak_allocated = 0;
        m_allocation_failures = 0;
        m_logging = nullptr;
    }

    bool D3D12_Srv_Descriptor_Allocator::allocate(
        D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle,
        D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle) noexcept
    {
        cpu_handle = {};
        gpu_handle = {};

        std::scoped_lock lock(m_mutex);
        if (!m_initialized.load(std::memory_order_acquire) ||
            m_heap.Get() == nullptr || m_free_indices.empty())
        {
            ++m_allocation_failures;
            if (m_logging != nullptr)
                m_logging->error("d3d12", "SRV descriptor allocation failed: heap exhausted or unavailable.");
            return false;
        }

        const auto index = m_free_indices.back();
        m_free_indices.pop_back();
        m_allocated_slots[index] = 1;
        ++m_allocated_count;
        m_peak_allocated = std::max(m_peak_allocated, m_allocated_count);

        const auto byte_offset = static_cast<std::uint64_t>(index) * m_descriptor_size;
        cpu_handle.ptr = m_cpu_start.ptr + static_cast<SIZE_T>(byte_offset);
        gpu_handle.ptr = m_gpu_start.ptr + byte_offset;
        return true;
    }

    bool D3D12_Srv_Descriptor_Allocator::release(
        const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
        const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.load(std::memory_order_acquire))
            return false;

        std::uint32_t index{};
        if (!resolve_index(cpu_handle, gpu_handle, index) ||
            m_allocated_slots[index] == 0)
        {
            if (m_logging != nullptr)
                m_logging->warning("d3d12", "Ignored invalid or duplicate SRV descriptor release.");
            return false;
        }

        m_allocated_slots[index] = 0;
        m_free_indices.push_back(index);
        if (m_allocated_count > 0)
            --m_allocated_count;
        return true;
    }

    void D3D12_Srv_Descriptor_Allocator::imgui_allocate(
        void* user_data,
        D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle,
        D3D12_GPU_DESCRIPTOR_HANDLE* gpu_handle) noexcept
    {
        if (cpu_handle != nullptr)
            *cpu_handle = {};
        if (gpu_handle != nullptr)
            *gpu_handle = {};

        if (user_data == nullptr || cpu_handle == nullptr || gpu_handle == nullptr)
            return;

        auto* allocator = static_cast<D3D12_Srv_Descriptor_Allocator*>(user_data);
        static_cast<void>(allocator->allocate(*cpu_handle, *gpu_handle));
    }

    void D3D12_Srv_Descriptor_Allocator::imgui_release(
        void* user_data,
        const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
        const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) noexcept
    {
        if (user_data == nullptr)
            return;

        auto* allocator = static_cast<D3D12_Srv_Descriptor_Allocator*>(user_data);
        static_cast<void>(allocator->release(cpu_handle, gpu_handle));
    }

    bool D3D12_Srv_Descriptor_Allocator::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }

    ID3D12DescriptorHeap* D3D12_Srv_Descriptor_Allocator::heap() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_heap.Get();
    }

    std::uint32_t D3D12_Srv_Descriptor_Allocator::capacity() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_capacity;
    }

    D3D12_Srv_Allocator_Metrics D3D12_Srv_Descriptor_Allocator::metrics() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return {
            m_capacity,
            m_allocated_count,
            static_cast<std::uint32_t>(m_free_indices.size()),
            m_peak_allocated,
            m_allocation_failures
        };
    }

    bool D3D12_Srv_Descriptor_Allocator::resolve_index(
        const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
        const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
        std::uint32_t& index) const noexcept
    {
        if (m_descriptor_size == 0 || m_capacity == 0 ||
            cpu_handle.ptr < m_cpu_start.ptr || gpu_handle.ptr < m_gpu_start.ptr)
        {
            return false;
        }

        const auto cpu_offset = static_cast<std::uint64_t>(cpu_handle.ptr - m_cpu_start.ptr);
        const auto gpu_offset = gpu_handle.ptr - m_gpu_start.ptr;
        if (cpu_offset != gpu_offset ||
            cpu_offset % m_descriptor_size != 0)
        {
            return false;
        }

        const auto resolved = cpu_offset / m_descriptor_size;
        if (resolved >= m_capacity ||
            resolved > std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }

        index = static_cast<std::uint32_t>(resolved);
        return true;
    }
}
