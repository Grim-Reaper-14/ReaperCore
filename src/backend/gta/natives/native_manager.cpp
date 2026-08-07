#include "reapercore/backend/gta/natives/native_manager.hpp"

#include "reapercore/backend/gta/gta_pointers.hpp"
#include "reapercore/backend/gta/natives/crossmap.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace reapercore
{
    namespace
    {
        // Minimal rage::scrProgram layout required by InitNativeTables.
        // Verified against the pinned YimMenuV2 scrProgram layout:
        // m_NativeCount at 0x2C and m_NativeEntrypoints at 0x40.
        struct Native_Table_Program final
        {
            std::uint8_t padding_00[0x2C]{};
            std::uint32_t native_count{};          // 0x2C
            std::uint8_t padding_30[0x10]{};
            Native_Handler* native_entrypoints{}; // 0x40
            std::uint8_t padding_48[0x38]{};
        };

        static_assert(offsetof(Native_Table_Program, native_count) == 0x2C);
        static_assert(offsetof(Native_Table_Program, native_entrypoints) == 0x40);
        static_assert(sizeof(Native_Table_Program) == 0x80);

        using Init_Native_Tables = void (*)(Native_Table_Program*);
    }

    bool Native_Manager::initialize(
        Logging_Manager& logging,
        const GTA_Pointers& pointers) noexcept
    {
        shutdown();

        if (!pointers.ready_for_natives())
        {
            logging.error("gta.natives", "Native manager initialization blocked: GTA pointers are not ready");
            return false;
        }

        if (!cache_handlers(logging, pointers))
            return false;

        m_initialized = true;
        logging.info(
            "gta.natives",
            "Native manager initialized",
            {{"handler_count", std::to_string(m_handlers.size())}});
        return true;
    }

    void Native_Manager::shutdown() noexcept
    {
        m_handlers.clear();
        m_handlers.shrink_to_fit();
        m_handlers_cached = false;
        m_initialized = false;
    }

    bool Native_Manager::initialized() const noexcept
    {
        return m_initialized;
    }

    bool Native_Manager::handlers_cached() const noexcept
    {
        return m_handlers_cached;
    }

    std::size_t Native_Manager::handler_count() const noexcept
    {
        return m_handlers.size();
    }

    Native_Handler Native_Manager::handler(const std::size_t index) const noexcept
    {
        if (!m_handlers_cached || index >= m_handlers.size())
            return nullptr;
        return m_handlers[index];
    }

    bool Native_Manager::cache_handlers(
        Logging_Manager& logging,
        const GTA_Pointers& pointers) noexcept
    {
        const auto init_address = pointers.init_native_tables();
        if (init_address == 0 || native_crossmap_size == 0)
        {
            logging.error("gta.natives", "Cannot cache native handlers: required native metadata is unavailable");
            return false;
        }

        m_handlers.assign(native_crossmap_size, nullptr);
        for (std::size_t index = 0; index < native_crossmap_size; ++index)
        {
            m_handlers[index] = reinterpret_cast<Native_Handler>(
                static_cast<std::uintptr_t>(g_native_crossmap[index]));
        }

        Native_Table_Program program{};
        program.native_count = static_cast<std::uint32_t>(m_handlers.size());
        program.native_entrypoints = m_handlers.data();

        const auto init_native_tables =
            reinterpret_cast<Init_Native_Tables>(init_address);
        init_native_tables(&program);

        const auto unresolved = std::count(m_handlers.begin(), m_handlers.end(), nullptr);
        if (unresolved != 0)
        {
            logging.error(
                "gta.natives",
                "Native handler cache validation failed",
                {{"unresolved", std::to_string(unresolved)},
                 {"total", std::to_string(m_handlers.size())}});
            m_handlers.clear();
            return false;
        }

        m_handlers_cached = true;
        logging.info(
            "gta.natives",
            "Native handlers cached and validated",
            {{"total", std::to_string(m_handlers.size())}});
        return true;
    }
}
