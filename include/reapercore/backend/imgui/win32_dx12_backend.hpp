#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>

#include <atomic>
#include <mutex>

struct ImGui_ImplDX12_InitInfo;

namespace reapercore
{
    class ImGui_Layer;
    class Logging_Manager;

    using ImGui_Srv_Descriptor_Allocate = void (*)(
        void* user_data,
        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle);

    using ImGui_Srv_Descriptor_Free = void (*)(
        void* user_data,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle);

    struct ImGui_Win32_DX12_Attach_Info
    {
        HWND window{};
        ID3D12Device* device{};
        ID3D12CommandQueue* command_queue{};
        ID3D12DescriptorHeap* srv_descriptor_heap{};
        int frames_in_flight{3};
        DXGI_FORMAT rtv_format{DXGI_FORMAT_R8G8B8A8_UNORM};
        DXGI_FORMAT dsv_format{DXGI_FORMAT_UNKNOWN};
        void* descriptor_user_data{};
        ImGui_Srv_Descriptor_Allocate allocate_srv_descriptor{};
        ImGui_Srv_Descriptor_Free free_srv_descriptor{};
    };

    struct ImGui_DX12_Frame_Context
    {
        ID3D12GraphicsCommandList* command_list{};
        ID3D12Resource* render_target{};
        D3D12_CPU_DESCRIPTOR_HANDLE render_target_view{};
        D3D12_RESOURCE_STATES state_before{D3D12_RESOURCE_STATE_PRESENT};
        D3D12_RESOURCE_STATES state_after{D3D12_RESOURCE_STATE_PRESENT};
        bool transition_render_target{};
        bool bind_render_target{};
    };

    class ImGui_Win32_DX12_Backend final
    {
    public:
        ImGui_Win32_DX12_Backend() = default;
        ~ImGui_Win32_DX12_Backend();

        ImGui_Win32_DX12_Backend(const ImGui_Win32_DX12_Backend&) = delete;
        ImGui_Win32_DX12_Backend& operator=(const ImGui_Win32_DX12_Backend&) = delete;

        bool initialize(Logging_Manager& logging, ImGui_Layer& layer) noexcept;
        void shutdown() noexcept;

        bool attach(const ImGui_Win32_DX12_Attach_Info& info) noexcept;
        void detach() noexcept;

        bool begin_frame() noexcept;
        bool render(ID3D12GraphicsCommandList* command_list) noexcept;
        bool render(const ImGui_DX12_Frame_Context& frame) noexcept;
        bool handle_window_message(
            HWND window,
            UINT message,
            WPARAM word_parameter,
            LPARAM long_parameter) noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] bool attached() const noexcept;
        [[nodiscard]] HWND window() const noexcept;

    private:
        static void allocate_descriptor_bridge(
            ImGui_ImplDX12_InitInfo* info,
            D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
            D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle);

        static void free_descriptor_bridge(
            ImGui_ImplDX12_InitInfo* info,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle);

        Logging_Manager* m_logging{};
        ImGui_Layer* m_layer{};
        ImGui_Win32_DX12_Attach_Info m_attach_info;

        mutable std::recursive_mutex m_mutex;
        std::atomic_bool m_initialized{false};
        std::atomic_bool m_attached{false};
    };
}
