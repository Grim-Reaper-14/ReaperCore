#include "reapercore/backend/gta/gta_runtime.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <Windows.h>

#include <string>

namespace reapercore
{
    namespace
    {
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

        m_initialized = true;
        logging.info("gta", "GTA Enhanced runtime is ready for native subsystem initialization.");
        return true;
    }

    void GTA_Runtime::shutdown() noexcept
    {
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
        return m_initialized && m_pointers.ready_for_natives();
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
}
