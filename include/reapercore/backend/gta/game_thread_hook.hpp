#pragma once

#include "reapercore/backend/gta/gta_pointers.hpp"
#include "reapercore/backend/hooking/detour_hook.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <cstdint>
#include <functional>
#include <intrin.h>
#include <utility>

namespace reapercore
{
    class Game_Thread_Hook final
    {
    public:
        using Tick_Callback = std::function<void()>;

        bool initialize(
            Logging_Manager& logging,
            const GTA_Pointers& pointers,
            Tick_Callback callback) noexcept
        {
            shutdown();

            if (pointers.run_script_threads() == 0 || pointers.script_threads() == 0)
            {
                logging.error(
                    "gta.script",
                    "Cannot install RunScriptThreads hook: required pointers are missing");
                return false;
            }

            m_logging = &logging;
            m_script_threads = pointers.script_threads();
            m_callback = std::move(callback);

            if (!m_hook.create(
                    logging,
                    "RunScriptThreads",
                    pointers.run_script_threads(),
                    reinterpret_cast<void*>(&Game_Thread_Hook::detour)))
            {
                shutdown();
                return false;
            }

            s_active = this;
            if (!m_hook.enable())
            {
                s_active = nullptr;
                shutdown();
                return false;
            }

            m_initialized = true;
            logging.info(
                "gta.script",
                "RunScriptThreads hook installed; callbacks will run in GTA script TLS context");
            return true;
        }

        void shutdown() noexcept
        {
            if (s_active == this)
                s_active = nullptr;

            m_hook.destroy();
            m_callback = {};
            m_script_threads = 0;
            m_initialized = false;
            m_logging = nullptr;
        }

        [[nodiscard]] bool initialized() const noexcept
        {
            return m_initialized;
        }

        [[nodiscard]] bool installed() const noexcept
        {
            return m_initialized && m_hook.enabled();
        }

    private:
        struct Script_Thread_Array final
        {
            void** data{};
            std::uint16_t size{};
            std::uint16_t capacity{};
            std::uint32_t padding{};
        };

        static_assert(sizeof(Script_Thread_Array) == 0x10);

        struct TLS_Context final
        {
            std::uint8_t padding_000[0x7A0]{};
            void* current_script_thread{}; // 0x7A0
            bool script_thread_active{};   // 0x7A8
            std::uint8_t padding_7A9[7]{};

            [[nodiscard]] static TLS_Context* get() noexcept
            {
                const auto tls_slots = __readgsqword(0x58);
                if (tls_slots == 0)
                    return nullptr;

                return *reinterpret_cast<TLS_Context**>(tls_slots);
            }
        };

        static_assert(sizeof(TLS_Context) == 0x7B0);

        using Run_Script_Threads = bool (*)(int);

        [[nodiscard]] void* find_script_thread() const noexcept
        {
            if (m_script_threads == 0)
                return nullptr;

            const auto* threads = reinterpret_cast<const Script_Thread_Array*>(m_script_threads);
            if (threads->data == nullptr || threads->size == 0 || threads->size > threads->capacity)
                return nullptr;

            for (std::uint16_t index = 0; index < threads->size; ++index)
            {
                if (threads->data[index] != nullptr)
                    return threads->data[index];
            }

            return nullptr;
        }

        static bool detour(const int ops_to_execute) noexcept
        {
            auto* active = s_active;
            if (active == nullptr)
                return false;

            return active->invoke(ops_to_execute);
        }

        bool invoke(const int ops_to_execute) noexcept
        {
            const auto original = m_hook.original_as<Run_Script_Threads>();
            if (original == nullptr)
                return false;

            // Match the current Enhanced execution model: let GTA finish its
            // own script pass first, then run ReaperCore work afterward.
            const bool result = original(ops_to_execute);

            if (!m_initialized || !m_callback)
                return result;

            void* script_thread = find_script_thread();
            auto* tls = TLS_Context::get();
            if (script_thread == nullptr || tls == nullptr)
                return result;

            void* previous_thread = tls->current_script_thread;
            const bool previous_active = tls->script_thread_active;

            tls->current_script_thread = script_thread;
            tls->script_thread_active = true;

            try
            {
                m_callback();
            }
            catch (...)
            {
                if (m_logging != nullptr)
                    m_logging->error("gta.script", "Unhandled exception in GTA script-thread callback");
            }

            tls->script_thread_active = previous_active;
            tls->current_script_thread = previous_thread;
            return result;
        }

        static inline Game_Thread_Hook* s_active{};

        Logging_Manager* m_logging{};
        Detour_Hook m_hook;
        Tick_Callback m_callback;
        std::uintptr_t m_script_threads{};
        bool m_initialized{};
    };
}
