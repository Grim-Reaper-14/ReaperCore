#include "reapercore/backend/imgui/dx12_texture_registry.hpp"
#include "reapercore/backend/d3d12/d3d12_backend.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <string>
#include <utility>

namespace reapercore
{
    ImGui_DX12_Texture_Registry::~ImGui_DX12_Texture_Registry()
    {
        shutdown();
    }

    bool ImGui_DX12_Texture_Registry::initialize(
        Logging_Manager& logging,
        D3D12_Backend& d3d12) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized)
            return true;

        m_logging = &logging;
        m_d3d12 = &d3d12;
        m_next_id = 1;
        m_registered_total = 0;
        m_unregistered_total = 0;
        m_registration_failures = 0;
        m_initialized = true;
        m_logging->info("imgui", "DX12 texture registry initialized.");
        return true;
    }

    void ImGui_DX12_Texture_Registry::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized)
            return;

        if (m_d3d12 != nullptr)
        {
            auto& allocator = m_d3d12->srv_descriptors();
            for (const auto& [_, entry] : m_entries)
            {
                static_cast<void>(allocator.release(
                    entry.cpu_descriptor,
                    entry.gpu_descriptor));
            }
        }

        m_unregistered_total += m_entries.size();
        m_entries.clear();

        if (m_logging != nullptr)
        {
            m_logging->info("imgui", "DX12 texture registry stopped.", {
                {"registered_total", std::to_string(m_registered_total)},
                {"unregistered_total", std::to_string(m_unregistered_total)},
                {"registration_failures", std::to_string(m_registration_failures)}
            });
        }

        m_d3d12 = nullptr;
        m_logging = nullptr;
        m_initialized = false;
    }

    std::optional<ImGui_DX12_Texture_Handle>
    ImGui_DX12_Texture_Registry::register_texture(
        ID3D12Resource& resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* description) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized || m_d3d12 == nullptr ||
            !m_d3d12->device_attached())
        {
            ++m_registration_failures;
            if (m_logging != nullptr)
                m_logging->error("imgui", "Cannot register a texture without an attached DX12 device.");
            return std::nullopt;
        }

        auto* device = m_d3d12->device();
        if (device == nullptr)
        {
            ++m_registration_failures;
            return std::nullopt;
        }

        auto& allocator = m_d3d12->srv_descriptors();
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor{};
        if (!allocator.allocate(cpu_descriptor, gpu_descriptor))
        {
            ++m_registration_failures;
            return std::nullopt;
        }

        device->CreateShaderResourceView(
            &resource,
            description,
            cpu_descriptor);

        try
        {
            ImGui_DX12_Texture_Id id = m_next_id++;
            while (id == 0 || m_entries.contains(id))
                id = m_next_id++;

            Entry entry;
            entry.resource = &resource;
            entry.cpu_descriptor = cpu_descriptor;
            entry.gpu_descriptor = gpu_descriptor;
            m_entries.emplace(id, std::move(entry));
            ++m_registered_total;

            return ImGui_DX12_Texture_Handle{id, gpu_descriptor};
        }
        catch (...)
        {
            static_cast<void>(allocator.release(
                cpu_descriptor,
                gpu_descriptor));
            ++m_registration_failures;
            if (m_logging != nullptr)
                m_logging->error("imgui", "DX12 texture registration failed while storing registry state.");
            return std::nullopt;
        }
    }

    bool ImGui_DX12_Texture_Registry::unregister_texture(
        const ImGui_DX12_Texture_Id id) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized || m_d3d12 == nullptr || id == 0)
            return false;

        const auto iterator = m_entries.find(id);
        if (iterator == m_entries.end())
            return false;

        const auto cpu_descriptor = iterator->second.cpu_descriptor;
        const auto gpu_descriptor = iterator->second.gpu_descriptor;
        m_entries.erase(iterator);
        ++m_unregistered_total;

        return m_d3d12->srv_descriptors().release(
            cpu_descriptor,
            gpu_descriptor);
    }

    void ImGui_DX12_Texture_Registry::clear() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized)
            return;

        if (m_d3d12 != nullptr)
        {
            auto& allocator = m_d3d12->srv_descriptors();
            for (const auto& [_, entry] : m_entries)
            {
                static_cast<void>(allocator.release(
                    entry.cpu_descriptor,
                    entry.gpu_descriptor));
            }
        }

        m_unregistered_total += m_entries.size();
        m_entries.clear();
    }

    std::optional<ImGui_DX12_Texture_Handle>
    ImGui_DX12_Texture_Registry::find(
        const ImGui_DX12_Texture_Id id) const noexcept
    {
        std::scoped_lock lock(m_mutex);
        const auto iterator = m_entries.find(id);
        if (iterator == m_entries.end())
            return std::nullopt;

        return ImGui_DX12_Texture_Handle{
            id,
            iterator->second.gpu_descriptor
        };
    }

    ImGui_DX12_Texture_Metrics
    ImGui_DX12_Texture_Registry::metrics() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return {
            m_registered_total,
            m_unregistered_total,
            m_registration_failures,
            m_entries.size()
        };
    }

    bool ImGui_DX12_Texture_Registry::initialized() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_initialized;
    }
}
