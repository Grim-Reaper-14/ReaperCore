#include "reapercore/backend/backend.hpp"
#include "reapercore/backend/hooking/detour_hook.hpp"
#include "reapercore/core/events/engine_events.hpp"
#include "reapercore/core/events/event_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"
#include "reapercore/core/lua/reapercore_lua_system.hpp"
#include "reapercore/core/settings_system/settings_system_manager.hpp"
#include "reapercore/core/tasks/task_manager.hpp"

#include <Windows.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reapercore
{
    namespace
    {
        using Present_Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using Execute_Command_Lists_Function = void(STDMETHODCALLTYPE*)(
            ID3D12CommandQueue*,
            UINT,
            ID3D12CommandList* const*);

        struct Present_Render_State final
        {
            Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain;
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue;
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
            Microsoft::WRL::ComPtr<ID3D12Fence> fence;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> render_targets;
            std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> allocators;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> render_target_views;
            std::vector<std::uint64_t> fence_values;
            HANDLE fence_event{};
            std::uint64_t next_fence_value{1};
            bool initialized{};
        };

        Detour_Hook g_present_hook;
        Detour_Hook g_execute_command_lists_hook;
        Backend* g_render_backend{};
        Logging_Manager* g_render_logging{};
        std::uintptr_t g_present_target{};
        std::uintptr_t g_execute_command_lists_target{};
        std::atomic<ID3D12CommandQueue*> g_last_direct_queue{nullptr};
        std::atomic_bool g_present_observed{false};
        std::atomic_bool g_direct_queue_observed{false};
        std::atomic_bool g_menu_input_capture{false};
        std::atomic_bool g_first_begin_frame_logged{false};
        std::atomic_bool g_first_begin_frame_success_logged{false};
        std::atomic_bool g_first_render_frame_logged{false};
        std::atomic_bool g_first_render_frame_success_logged{false};
        std::atomic_bool g_first_submit_logged{false};
        std::atomic_bool g_first_submit_success_logged{false};
        std::atomic<HWND> g_input_window{nullptr};
        std::atomic<WNDPROC> g_original_window_proc{nullptr};
        std::mutex g_present_render_mutex;
        Present_Render_State g_present_render_state;

        bool is_mouse_input_message(const UINT message) noexcept
        {
            switch (message)
            {
            case WM_MOUSEMOVE:
            case WM_MOUSELEAVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                return true;
            default:
                return false;
            }
        }

        bool is_keyboard_input_message(const UINT message) noexcept
        {
            switch (message)
            {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_DEADCHAR:
            case WM_SYSCHAR:
            case WM_SYSDEADCHAR:
            case WM_UNICHAR:
                return true;
            default:
                return false;
            }
        }

        void apply_menu_input_capture_state() noexcept
        {
            if (g_render_backend == nullptr ||
                !g_render_backend->imgui_backend().attached())
            {
                return;
            }

            const bool capture =
                g_menu_input_capture.load(std::memory_order_acquire);

            g_render_backend->imgui().make_current();
            ImGui::GetIO().MouseDrawCursor = capture;
        }

        void set_menu_input_capture(const bool capture) noexcept
        {
            const bool previous = g_menu_input_capture.exchange(
                capture,
                std::memory_order_acq_rel);
            apply_menu_input_capture_state();

            if (previous != capture && g_render_logging != nullptr)
            {
                g_render_logging->info(
                    "input",
                    capture
                        ? "ReaperCore menu captured game input."
                        : "ReaperCore menu released game input.");
            }
        }

        LRESULT CALLBACK render_window_proc(
            const HWND window,
            const UINT message,
            const WPARAM word_parameter,
            const LPARAM long_parameter)
        {
            // Feed every message to Dear ImGui, but never consume GTA's Win32
            // or raw-input stream here. GTA's front-end and gameplay input
            // state must remain owned by the game; control suppression belongs
            // at the GTA control layer instead of the window procedure.
            if (g_render_backend != nullptr &&
                g_render_backend->imgui_backend().attached())
            {
                static_cast<void>(g_render_backend->handle_imgui_window_message(
                    window,
                    message,
                    word_parameter,
                    long_parameter));
            }

            const WNDPROC original =
                g_original_window_proc.load(std::memory_order_acquire);
            return original != nullptr
                ? CallWindowProcW(
                    original,
                    window,
                    message,
                    word_parameter,
                    long_parameter)
                : DefWindowProcW(
                    window,
                    message,
                    word_parameter,
                    long_parameter);
        }

        void restore_input_window_proc() noexcept
        {
            const HWND window = g_input_window.exchange(
                nullptr,
                std::memory_order_acq_rel);
            const WNDPROC original = g_original_window_proc.exchange(
                nullptr,
                std::memory_order_acq_rel);

            if (window == nullptr || original == nullptr || !IsWindow(window))
                return;

            const auto current = reinterpret_cast<WNDPROC>(
                GetWindowLongPtrW(window, GWLP_WNDPROC));
            if (current == &render_window_proc)
            {
                SetLastError(ERROR_SUCCESS);
                const LONG_PTR result = SetWindowLongPtrW(
                    window,
                    GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(original));
                if (result == 0 && GetLastError() != ERROR_SUCCESS)
                {
                    if (g_render_logging != nullptr)
                        g_render_logging->error(
                            "input",
                            "Failed to restore GTA window procedure.", {
                                {"error", std::to_string(GetLastError())}
                            });
                    return;
                }
            }

            if (g_render_logging != nullptr)
                g_render_logging->info("input", "GTA window procedure restored.");
        }

        bool install_input_window_proc(const HWND window) noexcept
        {
            if (window == nullptr || !IsWindow(window))
                return false;

            const HWND current_window =
                g_input_window.load(std::memory_order_acquire);
            if (current_window == window &&
                g_original_window_proc.load(std::memory_order_acquire) != nullptr)
            {
                return true;
            }

            if (current_window != nullptr)
                restore_input_window_proc();

            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(
                window,
                GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&render_window_proc));
            if (previous == 0 && GetLastError() != ERROR_SUCCESS)
            {
                if (g_render_logging != nullptr)
                    g_render_logging->error(
                        "input",
                        "Failed to subclass GTA window procedure.", {
                            {"error", std::to_string(GetLastError())}
                        });
                return false;
            }

            const auto original = reinterpret_cast<WNDPROC>(previous);
            if (original == nullptr)
            {
                if (g_render_logging != nullptr)
                    g_render_logging->error(
                        "input",
                        "GTA window procedure was unexpectedly null.");
                return false;
            }

            g_original_window_proc.store(original, std::memory_order_release);
            g_input_window.store(window, std::memory_order_release);

            if (g_render_logging != nullptr)
                g_render_logging->info(
                    "input",
                    "GTA window procedure subclassed for ImGui input forwarding.");
            return true;
        }

        void release_present_render_state(Present_Render_State& state) noexcept
        {
            if (state.fence_event != nullptr)
                CloseHandle(state.fence_event);
            state = {};
        }

        bool discover_render_hook_targets(Logging_Manager& logging) noexcept
        {
            if (g_present_target != 0 && g_execute_command_lists_target != 0)
                return true;

            Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
            HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                logging.error("renderer", "Failed to create DXGI factory for render-hook discovery.", {
                    {"hresult", std::to_string(static_cast<long long>(result))}
                });
                return false;
            }

            Microsoft::WRL::ComPtr<ID3D12Device> device;
            result = D3D12CreateDevice(
                nullptr,
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                logging.error("renderer", "Failed to create temporary DX12 device for render-hook discovery.", {
                    {"hresult", std::to_string(static_cast<long long>(result))}
                });
                return false;
            }

            D3D12_COMMAND_QUEUE_DESC queue_description{};
            queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            queue_description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            queue_description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

            Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
            result = device->CreateCommandQueue(
                &queue_description,
                IID_PPV_ARGS(queue.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                logging.error("renderer", "Failed to create temporary DX12 queue for render-hook discovery.", {
                    {"hresult", std::to_string(static_cast<long long>(result))}
                });
                return false;
            }

            const HWND window = CreateWindowExW(
                0,
                L"STATIC",
                L"ReaperCore DXGI Probe",
                WS_OVERLAPPED,
                0,
                0,
                32,
                32,
                nullptr,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            if (window == nullptr)
            {
                logging.error("renderer", "Failed to create temporary window for render-hook discovery.");
                return false;
            }

            DXGI_SWAP_CHAIN_DESC1 swap_description{};
            swap_description.Width = 32;
            swap_description.Height = 32;
            swap_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swap_description.Stereo = FALSE;
            swap_description.SampleDesc.Count = 1;
            swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap_description.BufferCount = 2;
            swap_description.Scaling = DXGI_SCALING_STRETCH;
            swap_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swap_description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

            Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain;
            result = factory->CreateSwapChainForHwnd(
                queue.Get(),
                window,
                &swap_description,
                nullptr,
                nullptr,
                swap_chain.ReleaseAndGetAddressOf());

            if (FAILED(result))
            {
                DestroyWindow(window);
                logging.error("renderer", "Failed to create temporary DXGI swapchain for render-hook discovery.", {
                    {"hresult", std::to_string(static_cast<long long>(result))}
                });
                return false;
            }

            auto** swap_chain_vtable = *reinterpret_cast<void***>(swap_chain.Get());
            auto** queue_vtable = *reinterpret_cast<void***>(queue.Get());
            g_present_target = reinterpret_cast<std::uintptr_t>(swap_chain_vtable[8]);
            g_execute_command_lists_target = reinterpret_cast<std::uintptr_t>(queue_vtable[10]);

            swap_chain.Reset();
            DestroyWindow(window);

            if (g_present_target == 0 || g_execute_command_lists_target == 0)
            {
                logging.error("renderer", "DX12 render-hook discovery returned a null method target.");
                return false;
            }

            logging.info("renderer", "Resolved DX12 render-hook targets.", {
                {"present", std::to_string(g_present_target)},
                {"execute_command_lists", std::to_string(g_execute_command_lists_target)}
            });
            return true;
        }

        bool initialize_present_render_state(IDXGISwapChain* swap_chain) noexcept
        {
            if (swap_chain == nullptr || g_render_backend == nullptr || g_render_logging == nullptr)
                return false;

            ID3D12CommandQueue* captured_queue = g_last_direct_queue.load(std::memory_order_acquire);
            if (captured_queue == nullptr)
                return false;

            DXGI_SWAP_CHAIN_DESC description{};
            if (FAILED(swap_chain->GetDesc(&description)) ||
                description.OutputWindow == nullptr ||
                description.BufferCount == 0)
            {
                return false;
            }

            Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain3;
            if (FAILED(swap_chain->QueryInterface(IID_PPV_ARGS(swap_chain3.ReleaseAndGetAddressOf()))))
                return false;

            Microsoft::WRL::ComPtr<ID3D12Device> device;
            if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(device.ReleaseAndGetAddressOf()))))
                return false;

            Microsoft::WRL::ComPtr<ID3D12Device> queue_device;
            if (FAILED(captured_queue->GetDevice(IID_PPV_ARGS(queue_device.ReleaseAndGetAddressOf()))) ||
                queue_device.Get() != device.Get())
            {
                return false;
            }

            Present_Render_State candidate;
            candidate.swap_chain = swap_chain3;
            candidate.device = device;
            candidate.command_queue = captured_queue;

            D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_description{};
            rtv_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv_heap_description.NumDescriptors = description.BufferCount;
            rtv_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            HRESULT result = device->CreateDescriptorHeap(
                &rtv_heap_description,
                IID_PPV_ARGS(candidate.rtv_heap.ReleaseAndGetAddressOf()));
            if (FAILED(result))
                return false;

            const UINT descriptor_size = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtv =
                candidate.rtv_heap->GetCPUDescriptorHandleForHeapStart();

            candidate.render_targets.resize(description.BufferCount);
            candidate.allocators.resize(description.BufferCount);
            candidate.render_target_views.resize(description.BufferCount);
            candidate.fence_values.resize(description.BufferCount, 0);

            for (UINT index = 0; index < description.BufferCount; ++index)
            {
                result = swap_chain3->GetBuffer(
                    index,
                    IID_PPV_ARGS(candidate.render_targets[index].ReleaseAndGetAddressOf()));
                if (FAILED(result))
                {
                    release_present_render_state(candidate);
                    return false;
                }

                candidate.render_target_views[index] = rtv;
                device->CreateRenderTargetView(candidate.render_targets[index].Get(), nullptr, rtv);
                rtv.ptr += descriptor_size;

                result = device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(candidate.allocators[index].ReleaseAndGetAddressOf()));
                if (FAILED(result))
                {
                    release_present_render_state(candidate);
                    return false;
                }
            }

            result = device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                candidate.allocators.front().Get(),
                nullptr,
                IID_PPV_ARGS(candidate.command_list.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                release_present_render_state(candidate);
                return false;
            }
            candidate.command_list->Close();

            result = device->CreateFence(
                0,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(candidate.fence.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                release_present_render_state(candidate);
                return false;
            }

            candidate.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (candidate.fence_event == nullptr)
            {
                release_present_render_state(candidate);
                return false;
            }

            Backend_ImGui_DX12_Attach_Info attach_info;
            attach_info.window = description.OutputWindow;
            attach_info.device = device.Get();
            attach_info.command_queue = captured_queue;
            attach_info.frames_in_flight = static_cast<int>(description.BufferCount);
            attach_info.rtv_format = description.BufferDesc.Format;
            attach_info.dsv_format = DXGI_FORMAT_UNKNOWN;
            attach_info.srv_descriptor_capacity = 64;

            g_render_logging->info(
                "renderer",
                "Beginning high-level ImGui Win32/DX12 attachment.");
            if (!g_render_backend->attach_imgui_dx12(attach_info))
            {
                g_render_logging->error(
                    "renderer",
                    "High-level ImGui Win32/DX12 attachment failed.");
                release_present_render_state(candidate);
                return false;
            }
            g_render_logging->info(
                "renderer",
                "High-level ImGui Win32/DX12 attachment completed.");

            g_render_logging->info(
                "renderer",
                "Installing GTA window procedure for ImGui input.");
            if (!install_input_window_proc(description.OutputWindow))
            {
                g_render_logging->error(
                    "renderer",
                    "GTA window procedure installation failed.");
                g_render_backend->detach_imgui_dx12();
                release_present_render_state(candidate);
                return false;
            }
            g_render_logging->info(
                "renderer",
                "GTA window procedure installation completed.");

            // Do not capture GTA input at the Win32 layer. The frontend remains
            // responsible for menu visibility while the WndProc only forwards
            // messages to ImGui for observation.
            set_menu_input_capture(false);

            candidate.initialized = true;
            g_present_render_state = std::move(candidate);

            g_render_logging->info("renderer", "GTA DX12 Present renderer attached.", {
                {"buffers", std::to_string(description.BufferCount)},
                {"format", std::to_string(static_cast<int>(description.BufferDesc.Format))}
            });
            return true;
        }

        bool render_present_frame(IDXGISwapChain* swap_chain) noexcept
        {
            std::scoped_lock lock(g_present_render_mutex);

            if (!g_present_render_state.initialized)
            {
                if (!initialize_present_render_state(swap_chain))
                    return false;
            }

            auto& state = g_present_render_state;
            if (state.swap_chain.Get() != swap_chain ||
                state.command_queue == nullptr ||
                state.command_list == nullptr ||
                state.fence == nullptr ||
                g_render_backend == nullptr)
            {
                return false;
            }

            const UINT frame_index = state.swap_chain->GetCurrentBackBufferIndex();
            if (frame_index >= state.allocators.size() ||
                frame_index >= state.render_targets.size() ||
                frame_index >= state.render_target_views.size() ||
                frame_index >= state.fence_values.size())
            {
                return false;
            }

            const std::uint64_t pending_fence = state.fence_values[frame_index];
            if (pending_fence != 0 && state.fence->GetCompletedValue() < pending_fence)
            {
                if (FAILED(state.fence->SetEventOnCompletion(pending_fence, state.fence_event)))
                    return false;
                if (WaitForSingleObject(state.fence_event, INFINITE) != WAIT_OBJECT_0)
                    return false;
            }

            auto* allocator = state.allocators[frame_index].Get();
            if (allocator == nullptr || FAILED(allocator->Reset()))
                return false;

            if (FAILED(state.command_list->Reset(allocator, nullptr)))
                return false;

            const bool first_begin_log =
                !g_first_begin_frame_logged.exchange(true, std::memory_order_acq_rel);
            if (first_begin_log && g_render_logging != nullptr)
                g_render_logging->info("renderer", "Entering first ImGui backend frame.");

            if (!g_render_backend->begin_imgui_frame())
            {
                if (g_render_logging != nullptr)
                    g_render_logging->error("renderer", "ImGui backend frame begin failed.");
                state.command_list->Close();
                return false;
            }

            if (!g_first_begin_frame_success_logged.exchange(true, std::memory_order_acq_rel) &&
                g_render_logging != nullptr)
            {
                g_render_logging->info("renderer", "First ImGui backend frame began successfully.");
            }

            ImGui_DX12_Frame_Context frame;
            frame.command_list = state.command_list.Get();
            frame.render_target = state.render_targets[frame_index].Get();
            frame.render_target_view = state.render_target_views[frame_index];
            frame.state_before = D3D12_RESOURCE_STATE_PRESENT;
            frame.state_after = D3D12_RESOURCE_STATE_PRESENT;
            frame.transition_render_target = true;
            frame.bind_render_target = true;

            if (!g_first_render_frame_logged.exchange(true, std::memory_order_acq_rel) &&
                g_render_logging != nullptr)
            {
                g_render_logging->info("renderer", "Recording first ImGui DX12 frame.");
            }

            if (!g_render_backend->render_imgui_frame(frame))
            {
                if (g_render_logging != nullptr)
                    g_render_logging->error("renderer", "ImGui DX12 frame recording failed.");
                g_render_backend->imgui().cancel_frame();
                state.command_list->Close();
                return false;
            }

            if (!g_first_render_frame_success_logged.exchange(true, std::memory_order_acq_rel) &&
                g_render_logging != nullptr)
            {
                g_render_logging->info("renderer", "First ImGui DX12 frame recorded successfully.");
            }

            if (FAILED(state.command_list->Close()))
                return false;

            ID3D12CommandList* command_lists[] = {state.command_list.Get()};
            if (!g_first_submit_logged.exchange(true, std::memory_order_acq_rel) &&
                g_render_logging != nullptr)
            {
                g_render_logging->info("renderer", "Submitting first ImGui command list to GTA DX12 queue.");
            }
            state.command_queue->ExecuteCommandLists(1, command_lists);

            const std::uint64_t fence_value = state.next_fence_value++;
            if (FAILED(state.command_queue->Signal(state.fence.Get(), fence_value)))
                return false;

            state.fence_values[frame_index] = fence_value;
            if (!g_first_submit_success_logged.exchange(true, std::memory_order_acq_rel) &&
                g_render_logging != nullptr)
            {
                g_render_logging->info("renderer", "First ImGui command list submitted successfully.");
            }
            return true;
        }

        HRESULT STDMETHODCALLTYPE present_detour(
            IDXGISwapChain* swap_chain,
            const UINT sync_interval,
            const UINT flags)
        {
            if (!g_present_observed.exchange(true, std::memory_order_acq_rel) &&
                g_render_logging != nullptr)
            {
                g_render_logging->info("renderer", "Observed GTA DXGI Present call.");
            }

            static_cast<void>(render_present_frame(swap_chain));

            const auto original = g_present_hook.original_as<Present_Function>();
            return original != nullptr
                ? original(swap_chain, sync_interval, flags)
                : DXGI_ERROR_INVALID_CALL;
        }

        void STDMETHODCALLTYPE execute_command_lists_detour(
            ID3D12CommandQueue* queue,
            const UINT command_list_count,
            ID3D12CommandList* const* command_lists)
        {
            if (queue != nullptr && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            {
                g_last_direct_queue.store(queue, std::memory_order_release);
                if (!g_direct_queue_observed.exchange(true, std::memory_order_acq_rel) &&
                    g_render_logging != nullptr)
                {
                    g_render_logging->info("renderer", "Captured GTA DX12 direct command queue.");
                }
            }

            const auto original =
                g_execute_command_lists_hook.original_as<Execute_Command_Lists_Function>();
            if (original != nullptr)
                original(queue, command_list_count, command_lists);
        }

        bool register_render_hooks(
            Logging_Manager& logging,
            Hook_Registry& registry,
            Backend& backend)
        {
            if (!discover_render_hook_targets(logging))
                return false;

            g_render_backend = &backend;
            g_render_logging = &logging;

            Hook_Descriptor present_descriptor;
            present_descriptor.name = "DXGI Present";
            present_descriptor.install = [&logging]() {
                return g_present_hook.create(
                           logging,
                           "DXGI Present",
                           g_present_target,
                           reinterpret_cast<void*>(&present_detour)) &&
                    g_present_hook.enable();
            };
            present_descriptor.uninstall = []() {
                g_present_hook.destroy();
            };

            if (!registry.register_hook(std::move(present_descriptor)))
                return false;

            Hook_Descriptor execute_descriptor;
            execute_descriptor.name = "D3D12 ExecuteCommandLists";
            execute_descriptor.install = [&logging]() {
                return g_execute_command_lists_hook.create(
                           logging,
                           "D3D12 ExecuteCommandLists",
                           g_execute_command_lists_target,
                           reinterpret_cast<void*>(&execute_command_lists_detour)) &&
                    g_execute_command_lists_hook.enable();
            };
            execute_descriptor.uninstall = []() {
                g_execute_command_lists_hook.destroy();
            };

            return registry.register_hook(std::move(execute_descriptor));
        }

        void reset_render_hook_state() noexcept
        {
            std::scoped_lock lock(g_present_render_mutex);
            set_menu_input_capture(false);
            restore_input_window_proc();
            release_present_render_state(g_present_render_state);
            g_last_direct_queue.store(nullptr, std::memory_order_release);
            g_present_observed.store(false, std::memory_order_release);
            g_direct_queue_observed.store(false, std::memory_order_release);
            g_first_begin_frame_logged.store(false, std::memory_order_release);
            g_first_begin_frame_success_logged.store(false, std::memory_order_release);
            g_first_render_frame_logged.store(false, std::memory_order_release);
            g_first_render_frame_success_logged.store(false, std::memory_order_release);
            g_first_submit_logged.store(false, std::memory_order_release);
            g_first_submit_success_logged.store(false, std::memory_order_release);
            g_present_target = 0;
            g_execute_command_lists_target = 0;
            g_render_backend = nullptr;
            g_render_logging = nullptr;
        }
    }

    bool Backend::initialize(
        Logging_Manager& logging,
        Settings_System_Manager& settings,
        ReaperCore_Lua_System& lua,
        Event_Manager& events,
        Task_Manager& tasks) noexcept
    {
        if (running())
            return true;

        m_logging = &logging;
        m_settings = &settings;
        m_lua = &lua;
        m_events = &events;
        m_tasks = &tasks;
        m_tick_index = 0;

        const int tick_ms = settings.get_int("backend.tick_ms", 50);
        m_tick_interval = std::chrono::milliseconds(tick_ms > 0 ? tick_ms : 50);

        const int task_budget = settings.get_int("backend.main_thread_tasks_per_tick", 64);
        m_main_thread_task_budget =
            task_budget > 0 && task_budget <= 4096
                ? static_cast<std::size_t>(task_budget)
                : 64;

        if (!m_hooks.initialize(logging))
            goto initialize_failed;
        if (!m_d3d12.initialize(logging))
            goto initialize_failed_hooks;
        if (!m_imgui.initialize(logging))
            goto initialize_failed_d3d12;
        if (!m_imgui_backend.initialize(logging, m_imgui))
            goto initialize_failed_imgui;
        if (!m_imgui_textures.initialize(logging, m_d3d12))
            goto initialize_failed_imgui_backend;
        if (!m_renderer.initialize(logging, m_d3d12))
            goto initialize_failed_textures;
        if (!register_render_hooks(logging, m_hooks, *this))
            goto initialize_failed_renderer;
        if (!m_hooks.install_all())
            goto initialize_failed_render_hooks;

        m_running.store(true, std::memory_order_release);
        m_logging->info("backend", "Backend initialized.", {
            {"tick_ms", std::to_string(m_tick_interval.count())},
            {"main_thread_task_budget", std::to_string(m_main_thread_task_budget)},
            {"imgui_version", std::string(m_imgui.version())}
        });
        return true;

    initialize_failed_render_hooks:
        m_hooks.uninstall_all();
    initialize_failed_renderer:
        reset_render_hook_state();
        m_renderer.shutdown();
    initialize_failed_textures:
        m_imgui_textures.shutdown();
    initialize_failed_imgui_backend:
        m_imgui_backend.shutdown();
    initialize_failed_imgui:
        m_imgui.shutdown();
    initialize_failed_d3d12:
        m_d3d12.shutdown();
    initialize_failed_hooks:
        m_hooks.shutdown();
    initialize_failed:
        m_tasks = nullptr;
        m_events = nullptr;
        m_lua = nullptr;
        m_settings = nullptr;
        m_logging = nullptr;
        return false;
    }

    void Backend::run()
    {
        while (m_running.load(std::memory_order_acquire))
        {
            if ((GetAsyncKeyState(VK_END) & 1) != 0)
            {
                if (m_events != nullptr)
                    m_events->publish(Shutdown_Requested_Event{});
                request_stop();
            }

            if (m_tasks != nullptr)
                m_tasks->process_main_thread(m_main_thread_task_budget);

            if (m_events != nullptr)
            {
                m_events->process_deferred();
                Backend_Tick_Event tick_event;
                tick_event.tick_index = ++m_tick_index;
                m_events->publish(tick_event);
            }

            if (m_lua != nullptr)
                m_lua->tick();

            if (!m_imgui_backend.attached())
                m_renderer.render_frame();
            std::this_thread::sleep_for(m_tick_interval);
        }
    }

    void Backend::request_stop() noexcept
    {
        m_running.store(false, std::memory_order_release);
    }

    void Backend::shutdown() noexcept
    {
        request_stop();
        if (m_events != nullptr)
            m_events->process_deferred();

        m_hooks.uninstall_all();
        reset_render_hook_state();
        m_renderer.shutdown();
        detach_imgui_dx12();
        m_imgui_textures.shutdown();
        m_imgui_backend.shutdown();
        m_imgui.shutdown();
        m_d3d12.shutdown();
        m_hooks.shutdown();

        if (m_logging != nullptr)
            m_logging->info("backend", "Backend stopped.", {{"ticks", std::to_string(m_tick_index)}});

        m_tasks = nullptr;
        m_events = nullptr;
        m_lua = nullptr;
        m_settings = nullptr;
        m_logging = nullptr;
    }

    bool Backend::attach_imgui_dx12(const Backend_ImGui_DX12_Attach_Info& info) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        if (!running() || info.window == nullptr || info.device == nullptr ||
            info.command_queue == nullptr || info.frames_in_flight <= 0 ||
            info.srv_descriptor_capacity == 0)
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "Invalid high-level ImGui DX12 attachment data.");
            return false;
        }

        if (m_imgui_backend.attached())
            return m_imgui_backend.window() == info.window && m_d3d12.device() == info.device;

        const bool device_was_attached = m_d3d12.device_attached();
        if (device_was_attached && m_d3d12.device() != info.device)
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "ImGui cannot attach to a different active DX12 device.");
            return false;
        }

        if (m_logging != nullptr)
            m_logging->info("imgui", "Attaching GTA DX12 descriptor services.");

        D3D12_Device_Attach_Info device_info;
        device_info.device = info.device;
        device_info.srv_descriptor_capacity = info.srv_descriptor_capacity;
        if (!m_d3d12.attach_device(device_info))
            return false;

        if (m_logging != nullptr)
            m_logging->info("imgui", "GTA DX12 descriptor services attached.");

        m_imgui_owns_d3d12_attachment = !device_was_attached;
        auto& descriptors = m_d3d12.srv_descriptors();

        ImGui_Win32_DX12_Attach_Info backend_info;
        backend_info.window = info.window;
        backend_info.device = info.device;
        backend_info.command_queue = info.command_queue;
        backend_info.srv_descriptor_heap = descriptors.heap();
        backend_info.frames_in_flight = info.frames_in_flight;
        backend_info.rtv_format = info.rtv_format;
        backend_info.dsv_format = info.dsv_format;
        backend_info.descriptor_user_data = &descriptors;
        backend_info.allocate_srv_descriptor = &D3D12_Srv_Descriptor_Allocator::imgui_allocate;
        backend_info.free_srv_descriptor = &D3D12_Srv_Descriptor_Allocator::imgui_release;

        if (m_logging != nullptr)
            m_logging->info("imgui", "Calling Dear ImGui Win32/DX12 backend attach.");

        if (!m_imgui_backend.attach(backend_info))
        {
            if (m_logging != nullptr)
                m_logging->error("imgui", "Dear ImGui Win32/DX12 backend attach returned failure.");
            if (m_imgui_owns_d3d12_attachment)
                m_d3d12.detach_device();
            m_imgui_owns_d3d12_attachment = false;
            return false;
        }

        if (m_logging != nullptr)
            m_logging->info("imgui", "Dear ImGui Win32/DX12 backend attach returned successfully.");

        if (m_logging != nullptr)
            m_logging->info("imgui", "High-level ImGui DX12 bridge attached.", {
                {"frames_in_flight", std::to_string(info.frames_in_flight)},
                {"srv_descriptor_capacity", std::to_string(info.srv_descriptor_capacity)}
            });
        return true;
    }

    void Backend::detach_imgui_dx12() noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        const bool was_attached = m_imgui_backend.attached();
        m_imgui_textures.clear();
        m_imgui_backend.detach();
        if (m_imgui_owns_d3d12_attachment)
            m_d3d12.detach_device();
        m_imgui_owns_d3d12_attachment = false;
        if (was_attached && m_logging != nullptr)
            m_logging->info("imgui", "High-level ImGui DX12 bridge detached.");
    }

    bool Backend::begin_imgui_frame() noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_backend.begin_frame();
    }

    bool Backend::render_imgui_frame(const ImGui_DX12_Frame_Context& frame) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_backend.render(frame);
    }

    bool Backend::handle_imgui_window_message(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_backend.handle_window_message(window, message, word_parameter, long_parameter);
    }

    std::optional<ImGui_DX12_Texture_Handle> Backend::register_imgui_texture(ID3D12Resource& resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        if (!m_imgui_backend.attached())
            return std::nullopt;
        return m_imgui_textures.register_texture(resource, description);
    }

    bool Backend::unregister_imgui_texture(const ImGui_DX12_Texture_Id id) noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_textures.unregister_texture(id);
    }

    std::optional<ImGui_DX12_Texture_Handle> Backend::find_imgui_texture(const ImGui_DX12_Texture_Id id) const noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_textures.find(id);
    }

    ImGui_DX12_Texture_Metrics Backend::imgui_texture_metrics() const noexcept
    {
        std::scoped_lock lock(m_imgui_attachment_mutex);
        return m_imgui_textures.metrics();
    }

    bool Backend::running() const noexcept { return m_running.load(std::memory_order_acquire); }
    GTA_Runtime& Backend::gta() noexcept { return m_gta; }
    D3D12_Backend& Backend::d3d12() noexcept { return m_d3d12; }
    ImGui_Layer& Backend::imgui() noexcept { return m_imgui; }
    ImGui_Win32_DX12_Backend& Backend::imgui_backend() noexcept { return m_imgui_backend; }
    Renderer& Backend::renderer() noexcept { return m_renderer; }
    Hook_Registry& Backend::hooks() noexcept { return m_hooks; }
}
