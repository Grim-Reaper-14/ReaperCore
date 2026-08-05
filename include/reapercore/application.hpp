#pragma once

#include "reapercore/logger.hpp"

#include <Windows.h>

#include <atomic>

namespace reapercore
{
    class application final
    {
    public:
        explicit application(HMODULE module) noexcept;

        application(const application&) = delete;
        application& operator=(const application&) = delete;

        DWORD run();
        void request_stop() noexcept;

    private:
        HMODULE m_module{};
        std::atomic_bool m_running{true};
        logger m_logger;
    };
}
