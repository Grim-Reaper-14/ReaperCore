#include "reapercore/backend/d3d12/execution_core.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <string>
#include <utility>

namespace reapercore
{
    bool D3D12_Frame_Resource_Manager::initialize(
        Logging_Manager& logging,
        ID3D12Device& device,
        const std::uint32_t frame_count_value) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        if (frame_count_value == 0)
        {
            logging.error("d3d12", "Frame resource count must be greater than zero.");
            return false;
        }

        std::vector<D3D12_Frame_Context> frames;
        try
        {
            frames.resize(frame_count_value);
        }
        catch (...)
        {
            logging.error("d3d12", "Failed to allocate frame contexts.");
            return false;
        }

        for (auto& frame : frames)
        {
            const HRESULT allocator_result = device.CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(frame.allocator.ReleaseAndGetAddressOf()));
            if (FAILED(allocator_result))
            {
                logging.error("d3d12", "Failed to create frame command allocator.", {
                    {"hresult", std::to_string(static_cast<long long>(allocator_result))}
                });
                return false;
            }
        }

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
        const HRESULT list_result = device.CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            frames.front().allocator.Get(),
            nullptr,
            IID_PPV_ARGS(command_list.ReleaseAndGetAddressOf()));
        if (FAILED(list_result))
        {
            logging.error("d3d12", "Failed to create graphics command list.", {
                {"hresult", std::to_string(static_cast<long long>(list_result))}
            });
            return false;
        }

        const HRESULT close_result = command_list->Close();
        if (FAILED(close_result))
        {
            logging.error("d3d12", "Failed to close initial graphics command list.", {
                {"hresult", std::to_string(static_cast<long long>(close_result))}
            });
            return false;
        }

        m_logging = &logging;
        m_frames = std::move(frames);
        m_command_list = std::move(command_list);
        m_frame_index = frame_count_value - 1;
        m_initialized.store(true, std::memory_order_release);

        m_logging->info("d3d12", "Frame resource manager initialized.", {
            {"frame_count", std::to_string(frame_count_value)}
        });
        return true;
    }

    void D3D12_Frame_Resource_Manager::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.exchange(false, std::memory_order_acq_rel))
            return;

        m_command_list.Reset();
        m_frames.clear();
        m_frame_index = 0;

        if (m_logging != nullptr)
            m_logging->info("d3d12", "Frame resource manager stopped.");
        m_logging = nullptr;
    }

    bool D3D12_Frame_Resource_Manager::begin_frame(
        D3D12_Fence_Manager& fence_manager) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.load(std::memory_order_acquire) ||
            m_frames.empty() || m_command_list.Get() == nullptr)
        {
            return false;
        }

        m_frame_index = (m_frame_index + 1) % static_cast<std::uint32_t>(m_frames.size());
        auto& frame = m_frames[m_frame_index];

        if (frame.fence_value != 0 &&
            fence_manager.completed_value() < frame.fence_value &&
            !fence_manager.wait(frame.fence_value))
        {
            return false;
        }

        const HRESULT allocator_result = frame.allocator->Reset();
        if (FAILED(allocator_result))
        {
            if (m_logging != nullptr)
            {
                m_logging->error("d3d12", "Failed to reset frame command allocator.", {
                    {"hresult", std::to_string(static_cast<long long>(allocator_result))},
                    {"frame_index", std::to_string(m_frame_index)}
                });
            }
            return false;
        }

        const HRESULT list_result = m_command_list->Reset(frame.allocator.Get(), nullptr);
        if (FAILED(list_result))
        {
            if (m_logging != nullptr)
            {
                m_logging->error("d3d12", "Failed to reset graphics command list.", {
                    {"hresult", std::to_string(static_cast<long long>(list_result))},
                    {"frame_index", std::to_string(m_frame_index)}
                });
            }
            return false;
        }

        return true;
    }

    bool D3D12_Frame_Resource_Manager::close_frame(
        const std::uint64_t fence_value) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized.load(std::memory_order_acquire) ||
            m_frames.empty() || m_command_list.Get() == nullptr || fence_value == 0)
        {
            return false;
        }

        const HRESULT close_result = m_command_list->Close();
        if (FAILED(close_result))
        {
            if (m_logging != nullptr)
            {
                m_logging->error("d3d12", "Failed to close graphics command list.", {
                    {"hresult", std::to_string(static_cast<long long>(close_result))},
                    {"frame_index", std::to_string(m_frame_index)}
                });
            }
            return false;
        }

        m_frames[m_frame_index].fence_value = fence_value;
        return true;
    }

    ID3D12GraphicsCommandList* D3D12_Frame_Resource_Manager::command_list() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_command_list.Get();
    }

    D3D12_Frame_Context* D3D12_Frame_Resource_Manager::current_frame() noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_frames.empty() ? nullptr : &m_frames[m_frame_index];
    }

    std::uint32_t D3D12_Frame_Resource_Manager::frame_index() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_frame_index;
    }

    std::uint32_t D3D12_Frame_Resource_Manager::frame_count() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return static_cast<std::uint32_t>(m_frames.size());
    }

    bool D3D12_Frame_Resource_Manager::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }
}
