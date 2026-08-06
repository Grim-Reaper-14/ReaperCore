#include "reapercore/backend/d3d12/execution_core.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <limits>
#include <string>

namespace reapercore
{
    bool D3D12_Command_Queue_Manager::initialize(
        Logging_Manager& logging,
        ID3D12Device& device) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        D3D12_COMMAND_QUEUE_DESC description{};
        description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        description.NodeMask = 0;

        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        const HRESULT result = device.CreateCommandQueue(
            &description,
            IID_PPV_ARGS(queue.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            logging.error("d3d12", "Failed to create graphics command queue.", {
                {"hresult", std::to_string(static_cast<long long>(result))}
            });
            return false;
        }

        m_logging = &logging;
        m_queue = std::move(queue);
        m_initialized.store(true, std::memory_order_release);
        m_logging->info("d3d12", "Graphics command queue initialized.");
        return true;
    }

    void D3D12_Command_Queue_Manager::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.exchange(false, std::memory_order_acq_rel))
            return;

        m_queue.Reset();
        if (m_logging != nullptr)
            m_logging->info("d3d12", "Graphics command queue stopped.");
        m_logging = nullptr;
    }

    bool D3D12_Command_Queue_Manager::execute(
        ID3D12CommandList* const* lists,
        const std::size_t count) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.load(std::memory_order_acquire) ||
            m_queue.Get() == nullptr || lists == nullptr || count == 0 ||
            count > static_cast<std::size_t>(std::numeric_limits<UINT>::max()))
        {
            return false;
        }

        m_queue->ExecuteCommandLists(static_cast<UINT>(count), lists);
        return true;
    }

    ID3D12CommandQueue* D3D12_Command_Queue_Manager::queue() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_queue.Get();
    }

    bool D3D12_Command_Queue_Manager::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }
}
