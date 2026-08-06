#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace reapercore
{
    class D3D12_Backend;
    class Logging_Manager;

    using ImGui_DX12_Texture_Id = std::uint64_t;

    struct ImGui_DX12_Texture_Handle
    {
        ImGui_DX12_Texture_Id id{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return id != 0 && gpu_descriptor.ptr != 0;
        }

        [[nodiscard]] std::uint64_t imgui_texture_id() const noexcept
        {
            return gpu_descriptor.ptr;
        }
    };

    struct ImGui_DX12_Texture_Metrics
    {
        std::uint64_t registered_total{};
        std::uint64_t unregistered_total{};
        std::uint64_t registration_failures{};
        std::size_t active{};
    };

    class ImGui_DX12_Texture_Registry final
    {
    public:
        ImGui_DX12_Texture_Registry() = default;
        ~ImGui_DX12_Texture_Registry();

        ImGui_DX12_Texture_Registry(
            const ImGui_DX12_Texture_Registry&) = delete;
        ImGui_DX12_Texture_Registry& operator=(
            const ImGui_DX12_Texture_Registry&) = delete;

        bool initialize(
            Logging_Manager& logging,
            D3D12_Backend& d3d12) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] std::optional<ImGui_DX12_Texture_Handle> register_texture(
            ID3D12Resource& resource,
            const D3D12_SHADER_RESOURCE_VIEW_DESC* description = nullptr) noexcept;
        bool unregister_texture(ImGui_DX12_Texture_Id id) noexcept;
        void clear() noexcept;

        [[nodiscard]] std::optional<ImGui_DX12_Texture_Handle> find(
            ImGui_DX12_Texture_Id id) const noexcept;
        [[nodiscard]] ImGui_DX12_Texture_Metrics metrics() const noexcept;
        [[nodiscard]] bool initialized() const noexcept;

    private:
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor{};
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor{};
        };

        Logging_Manager* m_logging{};
        D3D12_Backend* m_d3d12{};
        mutable std::mutex m_mutex;
        std::unordered_map<ImGui_DX12_Texture_Id, Entry> m_entries;
        ImGui_DX12_Texture_Id m_next_id{1};
        std::uint64_t m_registered_total{};
        std::uint64_t m_unregistered_total{};
        std::uint64_t m_registration_failures{};
        bool m_initialized{};
    };
}
