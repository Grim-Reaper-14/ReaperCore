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
        void render_frame() noexcept