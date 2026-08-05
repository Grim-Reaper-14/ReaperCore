#pragma once

#include "reapercore/core.hpp"

#include <Windows.h>

namespace reapercore
{
    class application final
    {
    public:
        explicit application(HMODULE module) noexcept;

        application(const application&) = delete;
        application& operator=(const application&) = delete;

        DWORD run();

    private:
        HMODULE m_module{};
        Core m_core;
    };
}
