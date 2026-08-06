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
            return true;

        m_logging = &logging;

        HMODULE module = GetModuleHandleW(L"GTA5_Enhanced.exe");
        if (module == nullptr)
            module = GetModuleHandleW(nullptr);

        if (!inspect_module(module, m_module_base, m_module_size))
        {
            logging.error("gta", "Failed to inspect the GTA executable module.");
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

        std::string executable_name = "GTA executable";
        if (length != 0)
        {
            const std::wstring wide_name(module_name, length);
            executable_name.assign(wide_name.begin(), wide_name.end());
        }

        m_initialized = true;
        logging.info("gta", "GTA runtime module discovered.", {
            {"module", executable_name},
            {"base", std::to_string(m_module_base)},
            {"size", std::to_string(m_module_size)}
        });
        return true;
    }

    void GTA_Runtime::shutdown() noexcept
    {
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

    std::uintptr_t GTA_Runtime::module_base() const noexcept
    {
        return m_module_base;
    }

    std::size_t GTA_Runtime::module_size() const noexcept
    {
        return m_module_size;
    }
}
