#include "reapercore/frontend/frontend.hpp"

#include "reapercore/backend/backend.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

namespace reapercore
{
    bool Frontend::initialize(
        Logging_Manager& logging,
        Backend& backend,
        Unload_Callback unload_callback) noexcept
    {
        if (m_initialized)
            return true;

        m_logging = &logging;
        m_backend = &backend;
        m_unload_callback = std::move(unload_callback);

        m_draw_callback_id = backend.imgui().add_draw_callback([this]() {
            // Menu rendering will be owned here once the menu is moved from
            // ui/menu into frontend/menu.
        });

        if (m_draw_callback_id == 0)
        {
            m_unload_callback = {};
            m_backend = nullptr;
            m_logging = nullptr;
            return false;
        }

        m_initialized = true;
        logging.info("frontend", "Frontend initialized and owns the ImGui draw callback.");
        return true;
    }

    void Frontend::shutdown() noexcept
    {
        if (!m_initialized)
            return;

        if (m_backend != nullptr && m_draw_callback_id != 0)
            m_backend->imgui().remove_draw_callback(m_draw_callback_id);

        if (m_logging != nullptr)
            m_logging->info("frontend", "Frontend stopped.");

        m_draw_callback_id = 0;
        m_unload_callback = {};
        m_backend = nullptr;
        m_logging = nullptr;
        m_initialized = false;
    }

    bool Frontend::initialized() const noexcept
    {
        return m_initialized;
    }
}
