#pragma once

#include <atomic>
#include <cstdint>

namespace reapercore
{
    class D3D12_Backend;
    class Logging_Manager;

    struct Render_Statistics
    {
        std::uint64_t frame_count{};
        std::uint64_t skipped_frames{};
    };

    class Renderer final
    {
    public:
        bool initialize(Logging_Manager& logging, D3D12_Backend& d3d12) noexcept;
        void render_frame() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] Render_Statistics statistics() const noexcept;

    private:
        Logging_Manager* m_logging{};
        D3D12_Backend* m_d3d12{};
        std::atomic_bool m_ready{false};
        std::atomic_uint64_t m_frame_count{0};
        std::atomic_uint64_t m_skipped_frames{0};
    };
}
