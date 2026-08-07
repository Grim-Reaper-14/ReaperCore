#pragma once

#include <cstddef>
#include <cstdint>

// The generated header is downloaded from the pinned YimMenuV2 revision by
// CMake. ReaperCore keeps the source revision fixed so native indices cannot
// silently change underneath a build.
#include "reapercore_crossmap_upstream.hpp"

namespace reapercore
{
    inline constexpr auto& g_native_crossmap = YimMenu::g_Crossmap;
    inline constexpr std::size_t native_crossmap_size = g_native_crossmap.size();
}
