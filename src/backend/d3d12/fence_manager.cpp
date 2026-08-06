#include "reapercore/backend/d3d12/execution_core.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <string>

namespace reapercore
{
    bool D3D12_Fence_Manager::initialize(
        Logging_Manager& logging,
        ID3D12Device& device) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        const HRESULT fence_result = device.CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
        if (FAILED(fence_result))
        {
            logging.error("d3d12", "Failed to create GPU fence.", {
                {"hresult", std::to_string(static_cast<long long>(fence_result))}
            });
            return false;
        }

        const HANDLE event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event_handle == nullptr)
        {
            logging.error("d3d12", "Failed to create GPU fence event.", {
                {"win32_error", std::to_string(GetLastError())}
            });
            return false;
        }

        m_logging = &logging;
        m_fence = std::move(fence);
        m_event = event_handle;
        m_next_value.store(1, std::memory_order_release);
        m_initialized.store(true, std::memory_order_release);
        m_logging->info("d3d12", "Fence manager initialized.");
        return true;
    }

    void D3D12_Fence_Manager::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.exchange(false, std::memory_order_acq_rel))
            return;

        if (m_event != nullptr)
        {
            CloseHandle(m_event);
            m_event = nullptr;
        }

        m_fence.Reset();
        m_next_value.store(1, std::memory_order_release);

        if (m_logging != nullptr)
            m_logging->info("d3d12", "Fence manager stopped.");
        m_logging = nullptr;
    }

    std::uint64_t D3D12_Fence_Manager::signal(
        ID3D12CommandQueue& queue) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.load(std::memory_order_acquire) || m_fence.Get() == nullptr)
            return 0;

        const auto value = m_next_value.fetch_add(1, std::memory_order_acq_rel);
        const HRESULT result = queue.Signal(m_fence.Get(), value);
        if (FAILED(result))
        {
            if (m_logging != nullptr)
            {
                m_logging->error("d3d12", "Failed to signal GPU fence.", {
                    {"hresult", std::to_string(static_cast<long long>(result))},
                    {"fence_value", std::to_string(value)}
                });
            }
            return 0;
        }

        return value;
    }

    bool D3D12_Fence_Manager::wait(
        const std::uint64_t value,
        const std::uint32_t timeout_ms) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.load(std::memory_order_acquire) ||
            m_fence.Get() == nullptr || m_event == nullptr || value == 0)
        {
            return false;
        }

        if (m_fence->GetCompletedValue() >= value)
            return true;

        const HRESULT result = m_fence->SetEventOnCompletion(value, m_event);
        if (FAILED(result))
        {
            if (m_logging != nullptr)
            {
                m_logging->error("d3d12", "Failed to arm GPU fence event.", {
                    {"hresult", std::to_string(static_cast<long long>(result))},
                    {"fence_value", std::to_string(value)}
                });
            }
            return false;
        }

        const DWORD wait_result = WaitForSingleObject(m_event, timeout_ms);
        if (wait_result == WAIT_OBJECT_0)
            return true;

        if (m_logging != nullptr)
        {
            m_logging->error("d3d12", "GPU fence wait failed.", {
                {"wait_result", std::to_string(wait_result)},
                {"fence_value", std::to_string(value)}
            });
        }
        return false;
    }

    bool D3D12_Fence_Manager::flush(
        ID3D12CommandQueue& queue,
        const std::uint32_t timeout_ms) noexcept
    {
        const auto value = signal(queue);
        return value != 0 && wait(value, timeout_ms);
    }

    std::uint64_t D3D12_Fence_Manager::completed_value() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_fence.Get() != nullptr ? m_fence->GetCompletedValue() : 0;
    }

    bool D3D12_Fence_Manager::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }
}
