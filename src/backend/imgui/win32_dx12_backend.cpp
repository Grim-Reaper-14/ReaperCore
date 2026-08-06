#include "reapercore/backend/imgui/win32_dx12_backend.hpp"
#include "reapercore/backend/imgui/imgui_layer.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM word_parameter,
    LPARAM long_parameter);

namespace reapercore
{
    ImGui_Win32_DX12_Backend::~ImGui_Win32_DX12_Backend()
    {
        shutdown();
    }

    bool ImGui_Win32_DX12_Backend::initialize(
        Logging_Manager& logging,
        ImGui_Layer& layer) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        if (!layer.initialized())
        {
            logging.error(
                "imgui",
                "Win32/DX12 backend initialization requires an active ImGui context.");
            return false;
        }

        m_logging = &logging;
        m_layer = &layer;
        m_initialized.store(true, std::memory_order_release);
        m_logging->info("imgui", "Win32/DX12 backend adapter initialized.");
        return true;
    }

    void ImGui_Win32_DX12_Backend::shutdown() noexcept
    {
        if (!initialized())
            return;

        detach();

        std::scoped_lock lock(m_mutex);
        if (!m_initialized.exchange(false, std::memory_order_acq_rel))
            return;

        if (m_logging != nullptr)
            m_logging->info("imgui", "Win32/DX12 backend adapter stopped.");

        m_layer = nullptr;
        m_logging = nullptr;
    }

    bool ImGui_Win32_DX12_Backend::attach(
        const ImGui_Win32_DX12_Attach_Info& info) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!initialized() || attached() || m_layer == nullptr)
            return false;

        if (info.window == nullptr ||
            info.device == nullptr ||
            info.command_queue == nullptr ||
            info.srv_descriptor_heap == nullptr ||
            info.frames_in_flight <= 0 ||
            info.allocate_srv_descriptor == nullptr ||
            info.free_srv_descriptor == nullptr)
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "Invalid Win32/DX12 attachment data.");
            return false;
        }

        m_layer->make_current();
        if (!ImGui_ImplWin32_Init(info.window))
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "Dear ImGui Win32 backend failed to attach.");
            return false;
        }

        m_attach_info = info;

        ImGui_ImplDX12_InitInfo dx12_info;
        dx12_info.Device = info.device;
        dx12_info.CommandQueue = info.command_queue;
        dx12_info.NumFramesInFlight = info.frames_in_flight;
        dx12_info.RTVFormat = info.rtv_format;
        dx12_info.DSVFormat = info.dsv_format;
        dx12_info.UserData = this;
        dx12_info.SrvDescriptorHeap = info.srv_descriptor_heap;
        dx12_info.SrvDescriptorAllocFn = &ImGui_Win32_DX12_Backend::allocate_descriptor_bridge;
        dx12_info.SrvDescriptorFreeFn = &ImGui_Win32_DX12_Backend::free_descriptor_bridge;

        if (!ImGui_ImplDX12_Init(&dx12_info))
        {
            ImGui_ImplWin32_Shutdown();
            m_attach_info = {};
            if (m_logging != nullptr)
                m_logging->error("imgui", "Dear ImGui DX12 backend failed to attach.");
            return false;
        }

        m_attached.store(true, std::memory_order_release);
        if (m_logging != nullptr)
        {
            m_logging->info("imgui", "Dear ImGui Win32/DX12 backends attached.", {
                {"frames_in_flight", std::to_string(info.frames_in_flight)},
                {"rtv_format", std::to_string(static_cast<int>(info.rtv_format))}
            });
        }
        return true;
    }

    void ImGui_Win32_DX12_Backend::detach() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_attached.exchange(false, std::memory_order_acq_rel))
            return;

        if (m_layer != nullptr)
        {
            m_layer->make_current();
            if (m_layer->frame_active())
                m_layer->cancel_frame();
        }

        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        m_attach_info = {};

        if (m_logging != nullptr)
            m_logging->info("imgui", "Dear ImGui Win32/DX12 backends detached.");
    }

    bool ImGui_Win32_DX12_Backend::begin_frame() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!attached() || m_layer == nullptr)
            return false;

        m_layer->make_current();
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        return m_layer->begin_frame();
    }

    bool ImGui_Win32_DX12_Backend::render(
        ID3D12GraphicsCommandList* command_list) noexcept
    {
        ImGui_DX12_Frame_Context frame;
        frame.command_list = command_list;
        return render(frame);
    }

    bool ImGui_Win32_DX12_Backend::render(
        const ImGui_DX12_Frame_Context& frame) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!attached() || m_layer == nullptr || frame.command_list == nullptr ||
            !m_layer->frame_active())
        {
            return false;
        }

        if ((frame.transition_render_target && frame.render_target == nullptr) ||
            (frame.bind_render_target && frame.render_target_view.ptr == 0))
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "Invalid DX12 frame context for ImGui rendering.");
            return false;
        }

        m_layer->make_current();
        m_layer->end_frame();

        auto* draw_data = m_layer->draw_data();
        if (draw_data == nullptr)
            return false;

        if (frame.transition_render_target &&
            frame.state_before != D3D12_RESOURCE_STATE_RENDER_TARGET)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = frame.render_target;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = frame.state_before;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            frame.command_list->ResourceBarrier(1, &barrier);
        }

        if (frame.bind_render_target)
        {
            frame.command_list->OMSetRenderTargets(
                1,
                &frame.render_target_view,
                FALSE,
                nullptr);
        }

        ID3D12DescriptorHeap* descriptor_heaps[] = {
            m_attach_info.srv_descriptor_heap
        };
        frame.command_list->SetDescriptorHeaps(1, descriptor_heaps);
        ImGui_ImplDX12_RenderDrawData(draw_data, frame.command_list);

        if (frame.transition_render_target &&
            frame.state_after != D3D12_RESOURCE_STATE_RENDER_TARGET)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = frame.render_target;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = frame.state_after;
            frame.command_list->ResourceBarrier(1, &barrier);
        }

        return true;
    }

    bool ImGui_Win32_DX12_Backend::handle_window_message(
        const HWND window_handle,
        const UINT message,
        const WPARAM word_parameter,
        const LPARAM long_parameter) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!attached() || m_layer == nullptr)
            return false;

        m_layer->make_current();
        return ImGui_ImplWin32_WndProcHandler(
            window_handle,
            message,
            word_parameter,
            long_parameter) != 0;
    }

    bool ImGui_Win32_DX12_Backend::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }

    bool ImGui_Win32_DX12_Backend::attached() const noexcept
    {
        return m_attached.load(std::memory_order_acquire);
    }

    HWND ImGui_Win32_DX12_Backend::window() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_attach_info.window;
    }

    void ImGui_Win32_DX12_Backend::allocate_descriptor_bridge(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
    {
        if (out_cpu_handle != nullptr)
            out_cpu_handle->ptr = 0;
        if (out_gpu_handle != nullptr)
            out_gpu_handle->ptr = 0;

        if (info == nullptr || info->UserData == nullptr ||
            out_cpu_handle == nullptr || out_gpu_handle == nullptr)
        {
            return;
        }

        auto* backend = static_cast<ImGui_Win32_DX12_Backend*>(info->UserData);
        const auto callback = backend->m_attach_info.allocate_srv_descriptor;
        if (callback == nullptr)
            return;

        try
        {
            callback(
                backend->m_attach_info.descriptor_user_data,
                out_cpu_handle,
                out_gpu_handle);
        }
        catch (...)
        {
            out_cpu_handle->ptr = 0;
            out_gpu_handle->ptr = 0;
            if (backend->m_logging != nullptr)
                backend->m_logging->error(
                    "imgui",
                    "SRV descriptor allocation callback threw an exception.");
        }
    }

    void ImGui_Win32_DX12_Backend::free_descriptor_bridge(
        ImGui_ImplDX12_InitInfo* info,
        const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
        const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
    {
        if (info == nullptr || info->UserData == nullptr)
            return;

        auto* backend = static_cast<ImGui_Win32_DX12_Backend*>(info->UserData);
        const auto callback = backend->m_attach_info.free_srv_descriptor;
        if (callback == nullptr)
            return;

        try
        {
            callback(
                backend->m_attach_info.descriptor_user_data,
                cpu_handle,
                gpu_handle);
        }
        catch (...)
        {
            if (backend->m_logging != nullptr)
                backend->m_logging->error(
                    "imgui",
                    "SRV descriptor release callback threw an exception.");
        }
    }
}
