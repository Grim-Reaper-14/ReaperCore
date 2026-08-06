#include "reapercore/backend/d3d12/device_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <Windows.h>

#include <array>
#include <string>
#include <utility>

namespace
{
    std::string narrow(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
            return {};

        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 1)
            return {};

        std::string result(static_cast<std::size_t>(required - 1), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            result.data(),
            required,
            nullptr,
            nullptr);
        return result;
    }
}

namespace reapercore
{
    D3D12_Device_Manager::~D3D12_Device_Manager()
    {
        shutdown();
    }

    bool D3D12_Device_Manager::initialize(
        Logging_Manager& logging,
        D3D12_Device_Manager_Config config) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        m_logging = &logging;
        m_config = config;

        if (m_config.enable_debug_layer)
            static_cast<void>(enable_debug_layer());

        UINT factory_flags{};
#if defined(_DEBUG)
        if (m_config.enable_debug_layer)
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

        const HRESULT factory_result = CreateDXGIFactory2(
            factory_flags,
            IID_PPV_ARGS(m_factory.ReleaseAndGetAddressOf()));
        if (FAILED(factory_result))
        {
            m_logging->error("d3d12", "Failed to create DXGI factory.", {
                {"hresult", std::to_string(static_cast<long long>(factory_result))}
            });
            shutdown();
            return false;
        }

        if (!select_adapter() || !create_device())
        {
            shutdown();
            return false;
        }

        query_capabilities();
        m_initialized.store(true, std::memory_order_release);

        m_logging->info("d3d12", "D3D12 device manager initialized.", {
            {"adapter", m_capabilities.adapter_name},
            {"dedicated_vram_mb", std::to_string(m_capabilities.dedicated_video_memory / (1024ULL * 1024ULL))},
            {"software_adapter", m_capabilities.software_adapter ? "true" : "false"},
            {"feature_level", std::to_string(static_cast<unsigned int>(m_capabilities.feature_level))}
        });
        return true;
    }

    void D3D12_Device_Manager::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized.exchange(false, std::memory_order_acq_rel) &&
            m_logging != nullptr)
        {
            m_logging->info("d3d12", "D3D12 device manager stopped.");
        }

        m_device.Reset();
        m_adapter.Reset();
        m_factory.Reset();
        m_capabilities = {};
        m_config = {};
        m_logging = nullptr;
    }

    bool D3D12_Device_Manager::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }

    ID3D12Device* D3D12_Device_Manager::device() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_device.Get();
    }

    IDXGIFactory7* D3D12_Device_Manager::factory() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_factory.Get();
    }

    IDXGIAdapter4* D3D12_Device_Manager::adapter() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_adapter.Get();
    }

    const D3D12_Device_Capabilities& D3D12_Device_Manager::capabilities() const noexcept
    {
        return m_capabilities;
    }

    bool D3D12_Device_Manager::enable_debug_layer() noexcept
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        const HRESULT result = D3D12GetDebugInterface(
            IID_PPV_ARGS(debug.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            if (m_logging != nullptr)
                m_logging->warning("d3d12", "D3D12 debug layer is unavailable.");
            return false;
        }

        debug->EnableDebugLayer();
        if (m_logging != nullptr)
            m_logging->info("d3d12", "D3D12 debug layer enabled.");
        return true;
    }

    bool D3D12_Device_Manager::select_adapter() noexcept
    {
        if (m_factory.Get() == nullptr)
            return false;

        for (UINT index = 0;; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
            const HRESULT result = m_factory->EnumAdapterByGpuPreference(
                index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(candidate.ReleaseAndGetAddressOf()));
            if (result == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(result))
                continue;

            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(candidate->GetDesc1(&description)))
                continue;

            const bool software =
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            if (software && !m_config.allow_software_adapter)
                continue;

            if (FAILED(D3D12CreateDevice(
                    candidate.Get(),
                    m_config.minimum_feature_level,
                    __uuidof(ID3D12Device),
                    nullptr)))
            {
                continue;
            }

            if (SUCCEEDED(candidate.As(&m_adapter)))
                return true;
        }

        if (m_config.allow_software_adapter)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
            if (SUCCEEDED(m_factory->EnumWarpAdapter(
                    IID_PPV_ARGS(warp.ReleaseAndGetAddressOf()))) &&
                SUCCEEDED(warp.As(&m_adapter)))
            {
                return true;
            }
        }

        if (m_logging != nullptr)
            m_logging->error("d3d12", "No compatible DX12 adapter was found.");
        return false;
    }

    bool D3D12_Device_Manager::create_device() noexcept
    {
        if (m_adapter.Get() == nullptr)
            return false;

        const HRESULT result = D3D12CreateDevice(
            m_adapter.Get(),
            m_config.minimum_feature_level,
            IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            if (m_logging != nullptr)
            {
                m_logging->error("d3d12", "Failed to create D3D12 device.", {
                    {"hresult", std::to_string(static_cast<long long>(result))}
                });
            }
            return false;
        }
        return true;
    }

    void D3D12_Device_Manager::query_capabilities() noexcept
    {
        if (m_adapter.Get() == nullptr || m_device.Get() == nullptr)
            return;

        DXGI_ADAPTER_DESC3 adapter_description{};
        if (SUCCEEDED(m_adapter->GetDesc3(&adapter_description)))
        {
            m_capabilities.adapter_name = narrow(adapter_description.Description);
            m_capabilities.dedicated_video_memory = adapter_description.DedicatedVideoMemory;
            m_capabilities.dedicated_system_memory = adapter_description.DedicatedSystemMemory;
            m_capabilities.shared_system_memory = adapter_description.SharedSystemMemory;
            m_capabilities.software_adapter =
                (adapter_description.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0;
        }

        constexpr std::array requested_levels{
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels{};
        feature_levels.NumFeatureLevels = static_cast<UINT>(requested_levels.size());
        feature_levels.pFeatureLevelsRequested = requested_levels.data();
        feature_levels.MaxSupportedFeatureLevel = m_config.minimum_feature_level;
        if (SUCCEEDED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_FEATURE_LEVELS,
                &feature_levels,
                sizeof(feature_levels))))
        {
            m_capabilities.feature_level = feature_levels.MaxSupportedFeatureLevel;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &options5,
                sizeof(options5))))
        {
            m_capabilities.raytracing_tier = options5.RaytracingTier;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS6,
                &options6,
                sizeof(options6))))
        {
            m_capabilities.variable_rate_shading_tier =
                options6.VariableShadingRateTier;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7,
                &options7,
                sizeof(options7))))
        {
            m_capabilities.mesh_shader_tier = options7.MeshShaderTier;
            m_capabilities.sampler_feedback_tier = options7.SamplerFeedbackTier;
        }

        m_capabilities.srv_descriptor_increment =
            m_device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_capabilities.rtv_descriptor_increment =
            m_device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_capabilities.dsv_descriptor_increment =
            m_device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }
}
