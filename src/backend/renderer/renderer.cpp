#include "reapercore/backend/renderer/renderer.hpp"
#include "reapercore/backend/d3d12/d3d12_backend.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

namespace reapercore
{
    bool Renderer::initialize(Logging_Manager& logging, D3D12_Backend& d3d12) noexcept
    {
        m_logging = &logging;
        m_d3d12 = &d3d12;

        if (!m_d3d12->ready())
        {
            m_logging->error("renderer", "Renderer initialization failed because D3D12 is not ready.");
            return false;
        }

        m_ready.store(true, std::memory_order_release);
        m_logging->info("renderer", "Renderer coordinator initialized.");
        return true;
    }

    void Renderer::render_frame() noexcept
    {
        if (!ready() || m_d3d12 == nullptr || !m_d3d12->ready())
        {
            m_skipped_frames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        m_d3d12->begin_frame();
        m_d3d12->end_frame();
        m_frame_count.fetch_add(1, std::memory_order_relaxed);
    }

    void Renderer::shutdown() noexcept
    {
        const bool was_ready = m_ready.exchange(false, std::memory_order_acq_rel);
        if (was_ready && m_logging != nullptr)
            m_logging->info("renderer", "Renderer coordinator stopped.");

        m_d3d12 = nullptr;
        m_logging = nullptr;
    }

    bool Renderer::ready() const noexcept
    {
        return m_ready.load(std::memory_order_acquire);
    }

    Render_Statistics Renderer::statistics() const noexcept
    {
        return {
            m_frame_count.load(std::memory_order_relaxed),
            m_skipped_frames.load(std::memory_order_relaxed)
        };
    }
}
