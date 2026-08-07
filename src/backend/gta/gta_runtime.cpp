#include "reapercore/backend/gta/gta_runtime.hpp"
#include "reapercore/backend/gta/game_thread_hook.hpp"
#include "reapercore/backend/gta/natives/native_invoker.hpp"
#include "reapercore/core/logging/logging_manager.hpp"
#include "reapercore_native_indices.hpp"

#include <Windows.h>

#include <atomic>
#include <string>

namespace reapercore
{
    namespace
    {
        Game_Thread_Hook g_game_thread_hook;
        std::atomic_bool g_ran_native_smoke_test{false};

        [[nodiscard]] bool inspect_module(
            HMODULE module,
            std::uintptr_t& base,
            std::size_t& size) noexcept
        {
            if (module == nullptr)
                return false;

            const auto module_base = reinterpret_cast<std::uintptr_t>(module);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module_base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
                return false;

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                module_base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
                nt->OptionalHeader.SizeOfImage == 0)
            {
                return false;
            }

            base = module_base;
            size = static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage);
            return true;
        }
    }

    bool GTA_Runtime::initialize(Logging_Manager& logging) noexcept
    {
        if (m_initialized)
            return ready_for_natives();

        m_logging = &logging;

        HMODULE module = GetModuleHandleW(L"GTA5_Enhanced.exe");
        if (module == nullptr)
        {
            logging.error("gta", "GTA5_Enhanced.exe is not loaded. ReaperCore GTA runtime requires GTA V Enhanced.");
            m_logging = nullptr;
            return false;
        }

        if (!inspect_module(module, m_module_base, m_module_size))
        {
            logging.error("gta", "Failed to inspect GTA5_Enhanced.exe.");
            m_logging = nullptr;
            m_module_base = 0;
            m_module_size = 0;
            return false;
        }

        wchar_t module_name[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(
            module,
            module_name,
            static_cast<DWORD>(std::size(module_name)));

        std::string executable_name = "GTA5_Enhanced.exe";
        if (length != 0)
        {
            const std::wstring wide_name(module_name, length);
            executable_name.assign(wide_name.begin(), wide_name.end());
        }

        logging.info("gta", "GTA runtime module discovered.", {
            {"module", executable_name},
            {"base", std::to_string(m_module_base)},
            {"size", std::to_string(m_module_size)}
        });

        if (!m_pointers.initialize(logging, m_module_base, m_module_size))
        {
            logging.error("gta", "GTA runtime initialization stopped because native pointers are incomplete.");
            m_pointers.shutdown();
            m_module_size = 0;
            m_module_base = 0;
            m_logging = nullptr;
            return false;
        }

        if (!m_natives.initialize(logging, m_pointers))
        {
            logging.error("gta", "GTA runtime initialization stopped because native handlers could not be cached.");
            m_natives.shutdown();
            m_pointers.shutdown();
            m_module_size = 0;
            m_module_base = 0;
            m_logging = nullptr;
            return false;
        }

        g_ran_native_smoke_test.store(false, std::memory_order_release);
        if (!g_game_thread_hook.initialize(
                logging,
                m_pointers,
                [this, &logging]() noexcept {
                    if (g_ran_native_smoke_test.exchange(true, std::memory_order_acq_rel))
                        return;

                    logging.info(
                        "gta.script",
                        "ReaperCore entered a GTA script-thread TLS context successfully.");

                    Native_Invoker invoker(m_natives);
                    const auto player_id = invoker.invoke<int>(
                        generated_natives::player_id,
                        false);
                    const auto player_ped_id = invoker.invoke<int>(
                        generated_natives::player_ped_id,
                        false);
                    const auto game_timer = invoker.invoke<int>(
                        generated_natives::get_game_timer,
                        false);

                    if (!player_id || !player_ped_id || !game_timer)
                    {
                        logging.error(
                            "gta.natives",
                            "GTA native smoke test failed: one or more handlers could not be invoked.");
                        return;
                    }

                    logging.info(
                        "gta.natives",
                        "GTA native smoke test succeeded.",
                        {
                            {"player_id", std::to_string(*player_id)},
                            {"player_ped_id", std::to_string(*player_ped_id)},
                            {"game_timer", std::to_string(*game_timer)}
                        });
                }))
        {
            logging.error("gta", "GTA runtime initialization stopped because the game-thread hook could not be installed.");
            g_game_thread_hook.shutdown();
            m_natives.shutdown();
            m_pointers.shutdown();
            m_module_size = 0;
            m_module_base = 0;
            m_logging = nullptr;
            return false;
        }

        m_initialized = true;
        logging.info("gta", "GTA Enhanced runtime, native handler cache, and script-thread hook are ready.");
        return true;
    }

    void GTA_Runtime::shutdown() noexcept
    {
        g_game_thread_hook.shutdown();
        g_ran_native_smoke_test.store(false, std::memory_order_release);
        m_natives.shutdown();
        m_pointers.shutdown();

        if (m_initialized && m_logging != nullptr)
            m_logging->info("gta", "GTA runtime stopped.");

        m_initialized = false;
        m_module_size = 0;
        m_module_base = 0;
        m_logging = nullptr;
    }

    bool GTA_Runtime::initialized() const noexcept
    {
        return m_initialized;
    }

    bool GTA_Runtime::ready_for_natives() const noexcept
    {
        return m_initialized &&
            g_game_thread_hook.installed() &&
            m_pointers.ready_for_natives() &&
            m_natives.handlers_cached();
    }

    std::uintptr_t GTA_Runtime::module_base() const noexcept
    {
        return m_module_base;
    }

    std::size_t GTA_Runtime::module_size() const noexcept
    {
        return m_module_size;
    }

    GTA_Pointers& GTA_Runtime::pointers() noexcept
    {
        return m_pointers;
    }

    const GTA_Pointers& GTA_Runtime::pointers() const noexcept
    {
        return m_pointers;
    }

    Native_Manager& GTA_Runtime::natives() noexcept
    {
        return m_natives;
    }

    const Native_Manager& GTA_Runtime::natives() const noexcept
    {
        return m_natives;
    }
}
