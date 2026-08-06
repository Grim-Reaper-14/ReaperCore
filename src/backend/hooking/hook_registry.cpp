#include "reapercore/backend/hooking/hook_registry.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <algorithm>
#include <utility>

namespace reapercore
{
    bool Hook_Registry::initialize(Logging_Manager& logging) noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized)
            return true;

        m_logging = &logging;
        m_initialized = true;
        m_logging->info("hooks", "Hook registry initialized.");
        return true;
    }

    bool Hook_Registry::register_hook(Hook_Descriptor descriptor)
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized || descriptor.name.empty() || !descriptor.install || !descriptor.uninstall)
            return false;

        const auto duplicate = std::ranges::find_if(m_hooks, [&](const Hook_Descriptor& hook)
        {
            return hook.name == descriptor.name;
        });

        if (duplicate != m_hooks.end())
            return false;

        m_hooks.emplace_back(std::move(descriptor));
        return true;
    }

    bool Hook_Registry::install_all()
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized)
            return false;

        for (auto& hook : m_hooks)
        {
            if (hook.installed)
                continue;

            if (!hook.install())
            {
                if (m_logging != nullptr)
                    m_logging->error("hooks", "Hook installation failed.", {{"hook", hook.name}});
                return false;
            }

            hook.installed = true;
            if (m_logging != nullptr)
                m_logging->info("hooks", "Hook installed.", {{"hook", hook.name}});
        }

        return true;
    }

    void Hook_Registry::uninstall_all() noexcept
    {
        std::scoped_lock lock(m_mutex);
        for (auto iterator = m_hooks.rbegin(); iterator != m_hooks.rend(); ++iterator)
        {
            if (!iterator->installed)
                continue;

            try
            {
                iterator->uninstall();
                iterator->installed = false;
            }
            catch (...)
            {
                if (m_logging != nullptr)
                    m_logging->error("hooks", "Hook uninstall threw an exception.", {{"hook", iterator->name}});
            }
        }
    }

    void Hook_Registry::shutdown() noexcept
    {
        uninstall_all();

        std::scoped_lock lock(m_mutex);
        m_hooks.clear();
        m_initialized = false;
        if (m_logging != nullptr)
            m_logging->info("hooks", "Hook registry stopped.");
        m_logging = nullptr;
    }

    bool Hook_Registry::initialized() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_initialized;
    }

    std::size_t Hook_Registry::registered_count() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_hooks.size();
    }

    std::size_t Hook_Registry::installed_count() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return static_cast<std::size_t>(std::ranges::count_if(m_hooks, [](const Hook_Descriptor& hook)
        {
            return hook.installed;
        }));
    }

    bool Hook_Registry::contains(const std::string_view name) const
    {
        std::scoped_lock lock(m_mutex);
        return std::ranges::any_of(m_hooks, [&](const Hook_Descriptor& hook)
        {
            return hook.name == name;
        });
    }
}
