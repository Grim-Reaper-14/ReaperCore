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
        std::mutex g_present_render_mutex;
        Present_Render_State g_present_render_state;

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

            if (!g_render_backend->attach_imgui_dx12(attach_info))
            {
                release_present_render_state(candidate);
                return false;
            }

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

            if (!g_render_backend->begin_imgui_frame())
            {
                state.command_list->Close();
                return false;
            }

            ImGui_DX12_Frame_Context frame;
            frame.command_list = state.command_list.Get();
            frame.render_target = state.render_targets[frame_index].Get();
            frame.render_target_view = state.render_target_views[frame_index];
            frame.state_before = D3D12_RESOURCE_STATE_PRESENT;
            frame.state_after = D3D12_RESOURCE_STATE_PRESENT;
            frame.transition_render_target = true;
            frame.bind_render_target = true;

            if (!g_render_backend->render_imgui_frame(frame))
            {
                g_render_backend->imgui().cancel_frame();
                state.command_list->Close();
                return false;
            }

            if (FAILED(state.command_list->Close()))
                return false;

            ID3D12CommandList* command_lists[] = {state.command_list.Get()};
            state.command_queue->ExecuteCommandLists(1, command_lists);

            const std::uint64_t fence_value = state.next_fence_value++;
            if (FAILED(state.command_queue->Signal(state.fence.Get(), fence_value)))
                return false;

            state.fence_values[frame_index] = fence_value;
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
            release_present_render_state(g_present_render_state);
            g_last_direct_queue.store(nullptr, std::memory_order_release);
            g_present_observed.store(false, std::memory_order_release);
            g_direct_queue_observed.store(false, std::memory_order_release);
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

        D3D12_Device_Attach_Info device_info;
        device_info.device = info.device;
        device_info.srv_descriptor_capacity = info.srv_descriptor_capacity;
        if (!m_d3d12.attach_device(device_info))
            return false;

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

        if (!m_imgui_backend.attach(backend_info))
        {
            if (m_imgui_owns_d3d12_attachment)
                m_d3d12.detach_device();
            m_imgui_owns_d3d12_attachment = false;
            return false;
        }

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
