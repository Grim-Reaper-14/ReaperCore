#include "reapercore/backend/gta/gta_pointers.hpp"

#include "reapercore/backend/gta/pattern_scanner.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <Windows.h>

#include <cstring>
#include <string>

namespace reapercore
{
    namespace
    {
        [[nodiscard]] std::uintptr_t add(
            const std::uintptr_t address,
            const std::ptrdiff_t offset) noexcept
        {
            return static_cast<std::uintptr_t>(
                static_cast<std::intptr_t>(address) + offset);
        }

        [[nodiscard]] std::uintptr_t rip(
            const std::uintptr_t displacement_address) noexcept
        {
            std::int32_t displacement{};
            std::memcpy(
                &displacement,
                reinterpret_cast<const void*>(displacement_address),
                sizeof(displacement));
            return add(displacement_address + sizeof(displacement), displacement);
        }

        [[nodiscard]] bool committed_address(
            const std::uintptr_t address) noexcept
        {
            if (address == 0)
                return false;

            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(address),
                    &information,
                    sizeof(information)) != sizeof(information))
            {
                return false;
            }

            if (information.State != MEM_COMMIT ||
                (information.Protect & PAGE_GUARD) != 0 ||
                information.Protect == PAGE_NOACCESS)
            {
                return false;
            }

            return true;
        }

        [[nodiscard]] bool executable_address(
            const std::uintptr_t address) noexcept
        {
            if (!committed_address(address))
                return false;

            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(address),
                    &information,
                    sizeof(information)) != sizeof(information))
            {
                return false;
            }

            const DWORD protection = information.Protect & 0xFFU;
            return protection == PAGE_EXECUTE ||
                protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY;
        }
    }

    bool GTA_Pointers::initialize(
        Logging_Manager& logging,
        const std::uintptr_t module_base,
        const std::size_t module_size) noexcept
    {
        if (m_initialized)
            return ready_for_natives();

        m_logging = &logging;
        m_resolutions.clear();

        Pattern_Scanner scanner(module_base, module_size);

        const auto resolve = [this, &logging, &scanner](
            const char* name,
            const char* pattern,
            const bool required,
            const bool executable,
            const auto transform) -> std::uintptr_t
        {
            const auto match = scanner.find(pattern);
            if (!match)
            {
                m_resolutions.push_back({name, 0, required});
                if (required)
                    logging.error("gta", "Required GTA Enhanced pattern was not found.", {{"pointer", name}});
                else
                    logging.warning("gta", "Optional GTA Enhanced pattern was not found.", {{"pointer", name}});
                return 0;
            }

            const auto address = transform(*match);
            const bool valid = executable
                ? executable_address(address)
                : committed_address(address);

            if (!valid)
            {
                m_resolutions.push_back({name, 0, required});
                logging.error("gta", "GTA Enhanced pattern resolved to an invalid memory page.", {
                    {"pointer", name},
                    {"address", std::to_string(address)},
                    {"expected", executable ? "executable" : "committed"}
                });
                return 0;
            }

            m_resolutions.push_back({name, address, required});
            logging.info("gta", "Resolved GTA Enhanced runtime pointer.", {
                {"pointer", name},
                {"address", std::to_string(address)}
            });
            return address;
        };

        // These patterns and transforms track YimMenuV2's current Enhanced
        // pointer resolver. Keep this set intentionally limited to the native
        // execution path so unrelated game updates do not block startup.
        m_script_threads = resolve(
            "ScriptThreads",
            "48 8B 05 ? ? ? ? 48 89 34 F8 48 FF C7 48 39 FB 75 97",
            true,
            false,
            [](const std::uintptr_t match) { return rip(match + 3); });

        m_init_native_tables = resolve(
            "InitNativeTables",
            "EB 2A 0F 1F 40 00 48 8B 54 17 10",
            true,
            true,
            [](const std::uintptr_t match) { return add(match, -0x2A); });

        m_run_script_threads = resolve(
            "RunScriptThreads",
            "BE 40 5D C6 00",
            true,
            true,
            [](const std::uintptr_t match) { return add(match, -0xA); });

        m_script_globals = resolve(
            "ScriptGlobals",
            "48 8B 8E B8 00 00 00 48 8D 15 ? ? ? ? 49 89 D8",
            true,
            false,
            [](const std::uintptr_t match) { return rip(match + 10); });

        m_script_programs = resolve(
            "ScriptPrograms",
            "48 C7 84 C8 D8 00 00 00 00 00 00 00",
            true,
            false,
            [](const std::uintptr_t match) { return add(rip(match + 0x16), 0xD8); });

        m_initialized = true;

        if (!ready_for_natives())
        {
            logging.error("gta", "GTA Enhanced native runtime pointers are incomplete.");
            return false;
        }

        logging.info("gta", "GTA Enhanced native runtime pointers are ready.");
        return true;
    }

    void GTA_Pointers::shutdown() noexcept
    {
        m_initialized = false;
        m_init_native_tables = 0;
        m_run_script_threads = 0;
        m_script_globals = 0;
        m_script_programs = 0;
        m_script_threads = 0;
        m_resolutions.clear();
        m_logging = nullptr;
    }

    bool GTA_Pointers::initialized() const noexcept
    {
        return m_initialized;
    }

    bool GTA_Pointers::ready_for_natives() const noexcept
    {
        return m_script_threads != 0 &&
            m_script_programs != 0 &&
            m_script_globals != 0 &&
            m_run_script_threads != 0 &&
            m_init_native_tables != 0;
    }

    const std::vector<GTA_Pointers::Resolution>& GTA_Pointers::resolutions() const noexcept
    {
        return m_resolutions;
    }

    std::uintptr_t GTA_Pointers::script_threads() const noexcept { return m_script_threads; }
    std::uintptr_t GTA_Pointers::script_programs() const noexcept { return m_script_programs; }
    std::uintptr_t GTA_Pointers::script_globals() const noexcept { return m_script_globals; }
    std::uintptr_t GTA_Pointers::run_script_threads() const noexcept { return m_run_script_threads; }
    std::uintptr_t GTA_Pointers::init_native_tables() const noexcept { return m_init_native_tables; }
}
