#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace reapercore
{
    class Logging_Manager;

    struct D3D12_Device_Capabilities
    {
        std::string adapter_name;
        std::uint64_t dedicated_video_memory{};
        std::uint64_t dedicated_system_memory{};
        std::uint64_t shared_system_memory{};
        D3D_FEATURE_LEVEL feature_level{D3D_FEATURE_LEVEL_11_0};
        D3D12_RAYTRACING_TIER raytracing_tier{D3D12_RAYTRACING_TIER_NOT_SUPPORTED};
        D3D12_MESH_SHADER_TIER mesh_shader_tier{D3D12_MESH_SHADER_TIER_NOT_SUPPORTED};
        D3D12_SAMPLER_FEEDBACK_TIER sampler_feedback_tier{D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED};
        D3D12_VARIABLE_SHADING_RATE_TIER variable_rate_shading_tier{D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED};
        std::uint32_t srv_descriptor_increment{};
        std::uint32_t rtv_descriptor_increment{};
        std::uint32_t dsv_descriptor_increment{};
        bool software_adapter{};
    };

    struct D3D12_Device_Manager_Config
    {
        bool enable_debug_layer{};
        bool allow_software_adapter{};
        D3D_FEATURE_LEVEL minimum_feature_level{D3D_FEATURE_LEVEL_11_0};
    };

    class D3D12_Device_Manager final
    {
    public:
        D3D12_Device_Manager() = default;
        ~D3D12_Device_Manager();

        D3D12_Device_Manager(const D3D12_Device_Manager&) = delete;
        D3D12_Device_Manager& operator=(const D3D12_Device_Manager&) = delete;

        bool initialize(
            Logging_Manager& logging,
            D3D12_Device_Manager_Config config = {}) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] ID3D12Device* device() const noexcept;
        [[nodiscard]] IDXGIFactory7* factory() const noexcept;
        [[nodiscard]] IDXGIAdapter4* adapter() const noexcept;
        [[nodiscard]] const D3D12_Device_Capabilities& capabilities() const noexcept;

    private:
        bool enable_debug_layer() noexcept;
        bool select_adapter() noexcept;
        bool create_device() noexcept;
        void query_capabilities() noexcept;

        Logging_Manager* m_logging{};
        D3D12_Device_Manager_Config m_config;
        D3D12_Device_Capabilities m_capabilities;
        Microsoft::WRL::ComPtr<IDXGIFactory7> m_factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter;
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        mutable std::mutex m_mutex;
        std::atomic_bool m_initialized{false};
    };
}
