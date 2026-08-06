#pragma once

#include <Windows.h>
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

    class D3D12_Fence_Manager final
    {
    public:
        bool initialize(Logging_Manager& logging, ID3D12Device& device) noexcept;
        void shutdown() noexcept;
        [[nodiscard]] std::uint64_t signal(ID3D12CommandQueue& queue) noexcept;
        bool wait(std::uint64_t value, std::uint32_t timeout_ms = INFINITE) noexcept;
        bool flush(ID3D12CommandQueue& queue, std::uint32_t timeout_ms = INFINITE) noexcept;
        [[nodiscard]] std::uint64_t completed_value() const noexcept;
        [[nodiscard]] bool initialized() const noexcept;

    private:
        Logging_Manager* m_logging{};
        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
        HANDLE m_event{};
        std::atomic_uint64_t m_next_value{1};
        std::atomic_bool m_initialized{false};
        mutable std::mutex m_mutex;
    };

    class D3D12_Command_Queue_Manager final
    {
    public:
        bool initialize(Logging_Manager& logging, ID3D12Device& device) noexcept;
        void shutdown() noexcept;
        bool execute(ID3D12CommandList* const* lists, std::size_t count) noexcept;
        [[nodiscard]] ID3D12CommandQueue* queue() const noexcept;
        [[nodiscard]] bool initialized() const noexcept;

    private:
        Logging_Manager* m_logging{};
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
        std::atomic_bool m_initialized{false};
        mutable std::mutex m_mutex;
    };

    struct D3D12_Frame_Context
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        std::uint64_t fence_value{};
    };

    class D3D12_Frame_Resource_Manager final
    {
    public:
        bool initialize(
            Logging_Manager& logging,
            ID3D12Device& device,
            std::uint32_t frame_count = 3) noexcept;
        void shutdown() noexcept;
        bool begin_frame(D3D12_Fence_Manager& fence_manager) noexcept;
        bool close_frame() noexcept;
        bool mark_submitted(std::uint64_t fence_value) noexcept;
        [[nodiscard]] ID3D12GraphicsCommandList* command_list() const noexcept;
        [[nodiscard]] D3D12_Frame_Context* current_frame() noexcept;
        [[nodiscard]] std::uint32_t frame_index() const noexcept;
        [[nodiscard]] std::uint32_t frame_count() const noexcept;
        [[nodiscard]] bool initialized() const noexcept;

    private:
        Logging_Manager* m_logging{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list;
        std::vector<D3D12_Frame_Context> m_frames;
        std::uint32_t m_frame_index{};
        std::atomic_bool m_initialized{false};
        mutable std::mutex m_mutex;
    };
}
