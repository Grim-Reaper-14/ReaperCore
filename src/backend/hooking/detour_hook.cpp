#include "reapercore/backend/hooking/detour_hook.hpp"

#include "reapercore/core/logging/logging_manager.hpp"

#include <MinHook.h>

#include <string>

namespace reapercore
{
    namespace
    {
        bool ensure_minhook_initialized(Logging_Manager& logging) noexcept
        {
            static bool initialized = false;
            if (initialized)
                return true;

            const auto status = MH_Initialize();
            if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
            {
                logging.error("hooking", "MinHook initialization failed", {
                    {"status", std::to_string(static_cast<int>(status))}
                });
                return false;
            }

            initialized = true;
            return true;
        }
    }

    bool Detour_Hook::create(
        Logging_Manager& logging,
        const char* name,
        const std::uintptr_t target,
        void* detour) noexcept
    {
        destroy();

        if (target == 0 || detour == nullptr)
        {
            logging.error("hooking", "Cannot create detour with null target or detour", {
                {"hook", name != nullptr ? name : "unnamed"}
            });
            return false;
        }

        if (!ensure_minhook_initialized(logging))
            return false;

        m_logging = &logging;
        m_name = name;
        m_target = reinterpret_cast<void*>(target);

        const auto status = MH_CreateHook(m_target, detour, &m_original);
        if (status != MH_OK)
        {
            logging.error("hooking", "MinHook failed to create detour", {
                {"hook", m_name != nullptr ? m_name : "unnamed"},
                {"status", std::to_string(static_cast<int>(status))}
            });
            m_target = nullptr;
            m_original = nullptr;
            m_logging = nullptr;
            m_name = nullptr;
            return false;
        }

        m_created = true;
        logging.info("hooking", "Detour created", {
            {"hook", m_name != nullptr ? m_name : "unnamed"}
        });
        return true;
    }

    bool Detour_Hook::enable() noexcept
    {
        if (!m_created || m_target == nullptr)
            return false;
        if (m_enabled)
            return true;

        const auto status = MH_EnableHook(m_target);
        if (status != MH_OK)
        {
            if (m_logging != nullptr)
            {
                m_logging->error("hooking", "MinHook failed to enable detour", {
                    {"hook", m_name != nullptr ? m_name : "unnamed"},
                    {"status", std::to_string(static_cast<int>(status))}
                });
            }
            return false;
        }

        m_enabled = true;
        return true;
    }

    void Detour_Hook::disable() noexcept
    {
        if (!m_created || !m_enabled || m_target == nullptr)
            return;

        MH_DisableHook(m_target);
        m_enabled = false;
    }

    void Detour_Hook::destroy() noexcept
    {
        if (m_created && m_target != nullptr)
        {
            disable();
            MH_RemoveHook(m_target);
        }

        m_created = false;
        m_enabled = false;
        m_target = nullptr;
        m_original = nullptr;
        m_logging = nullptr;
        m_name = nullptr;
    }

    bool Detour_Hook::created() const noexcept
    {
        return m_created;
    }

    bool Detour_Hook::enabled() const noexcept
    {
        return m_enabled;
    }

    void* Detour_Hook::original() const noexcept
    {
        return m_original;
    }
}
