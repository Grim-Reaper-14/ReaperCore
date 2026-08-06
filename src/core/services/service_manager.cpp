#include "reapercore/core/services/service_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace reapercore
{
    Service_Manager::Service_Manager(Logging_Manager& logging) noexcept :
        m_logging(&logging)
    {
    }

    Service_Manager::~Service_Manager()
    {
        shutdown_all();
    }

    bool Service_Manager::register_service(std::unique_ptr<Service> service)
    {
        if (!service)
            return false;

        const std::string service_name(service->name());
        if (service_name.empty())
            return false;

        std::scoped_lock lock(m_mutex);
        if (m_initialized || m_services.contains(service_name))
            return false;

        Entry entry;
        entry.service = std::move(service);
        entry.state = Service_State::registered;
        m_services.emplace(service_name, std::move(entry));

        if (m_logging != nullptr)
            m_logging->info("services", "Service registered.", {{"service", service_name}});

        return true;
    }

    bool Service_Manager::initialize_all()
    {
        std::scoped_lock lock(m_mutex);
        if (m_initialized)
            return true;

        std::vector<std::string> order;
        if (!build_start_order(order))
            return false;

        std::vector<std::string> initialized;
        initialized.reserve(order.size());

        for (const auto& name : order)
        {
            auto& entry = m_services.at(name);
            entry.state = Service_State::initializing;

            try
            {
                if (!entry.service->initialize())
                {
                    entry.state = Service_State::failed;
                    if (m_logging != nullptr)
                        m_logging->error("services", "Service initialization failed.", {{"service", name}});
                    rollback_initialization(initialized);
                    return false;
                }
            }
            catch (const std::exception& exception)
            {
                entry.state = Service_State::failed;
                if (m_logging != nullptr)
                    m_logging->log_exception("services", exception, "Service initialization threw an exception.");
                rollback_initialization(initialized);
                return false;
            }
            catch (...)
            {
                entry.state = Service_State::failed;
                if (m_logging != nullptr)
                    m_logging->critical("services", "Service initialization threw an unknown exception.", {{"service", name}});
                rollback_initialization(initialized);
                return false;
            }

            entry.state = Service_State::initialized;
            initialized.push_back(name);

            if (m_logging != nullptr)
                m_logging->info("services", "Service initialized.", {{"service", name}});
        }

        m_order = std::move(order);
        m_initialized = true;
        return true;
    }

    bool Service_Manager::start_all()
    {
        std::scoped_lock lock(m_mutex);
        if (m_started)
            return true;
        if (!m_initialized && !initialize_all())
            return false;

        std::vector<std::string> started;
        started.reserve(m_order.size());

        for (const auto& name : m_order)
        {
            auto& entry = m_services.at(name);
            entry.state = Service_State::starting;

            try
            {
                if (!entry.service->start())
                {
                    entry.state = Service_State::failed;
                    if (m_logging != nullptr)
                        m_logging->error("services", "Service start failed.", {{"service", name}});
                    rollback_start(started);
                    return false;
                }
            }
            catch (const std::exception& exception)
            {
                entry.state = Service_State::failed;
                if (m_logging != nullptr)
                    m_logging->log_exception("services", exception, "Service start threw an exception.");
                rollback_start(started);
                return false;
            }
            catch (...)
            {
                entry.state = Service_State::failed;
                if (m_logging != nullptr)
                    m_logging->critical("services", "Service start threw an unknown exception.", {{"service", name}});
                rollback_start(started);
                return false;
            }

            entry.state = Service_State::running;
            started.push_back(name);

            if (m_logging != nullptr)
                m_logging->info("services", "Service started.", {{"service", name}});
        }

        m_started = true;
        return true;
    }

    void Service_Manager::tick_all()
    {
        std::scoped_lock lock(m_mutex);
        if (!m_started)
            return;

        for (const auto& name : m_order)
        {
            auto& entry = m_services.at(name);
            if (entry.state != Service_State::running)
                continue;

            try
            {
                entry.service->tick();
            }
            catch (const std::exception& exception)
            {
                entry.state = Service_State::failed;
                if (m_logging != nullptr)
                    m_logging->log_exception("services", exception, "Service tick threw an exception.");
            }
            catch (...)
            {
                entry.state = Service_State::failed;
                if (m_logging != nullptr)
                    m_logging->critical("services", "Service tick threw an unknown exception.", {{"service", name}});
            }
        }
    }

    void Service_Manager::stop_all() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_started)
            return;

        for (auto iterator = m_order.rbegin(); iterator != m_order.rend(); ++iterator)
        {
            auto& entry = m_services.at(*iterator);
            if (entry.state != Service_State::running && entry.state != Service_State::failed)
                continue;

            entry.state = Service_State::stopping;
            try
            {
                entry.service->stop();
            }
            catch (...)
            {
                if (m_logging != nullptr)
                    m_logging->error("services", "Service stop threw an exception.", {{"service", *iterator}});
            }
            entry.state = Service_State::stopped;
        }

        m_started = false;
    }

    void Service_Manager::shutdown_all() noexcept
    {
        std::scoped_lock lock(m_mutex);
        stop_all();

        for (auto iterator = m_order.rbegin(); iterator != m_order.rend(); ++iterator)
        {
            auto& entry = m_services.at(*iterator);
            if (entry.state == Service_State::registered)
                continue;

            try
            {
                entry.service->shutdown();
            }
            catch (...)
            {
                if (m_logging != nullptr)
                    m_logging->error("services", "Service shutdown threw an exception.", {{"service", *iterator}});
            }
            entry.state = Service_State::registered;
        }

        m_order.clear();
        m_initialized = false;
    }

    Service* Service_Manager::find(const std::string_view name) noexcept
    {
        std::scoped_lock lock(m_mutex);
        const auto iterator = m_services.find(std::string(name));
        return iterator == m_services.end() ? nullptr : iterator->second.service.get();
    }

    const Service* Service_Manager::find(const std::string_view name) const noexcept
    {
        std::scoped_lock lock(m_mutex);
        const auto iterator = m_services.find(std::string(name));
        return iterator == m_services.end() ? nullptr : iterator->second.service.get();
    }

    std::optional<Service_Status> Service_Manager::status(const std::string_view name) const
    {
        std::scoped_lock lock(m_mutex);
        const auto iterator = m_services.find(std::string(name));
        if (iterator == m_services.end())
            return std::nullopt;

        Service_Status result;
        result.name = iterator->first;
        result.state = iterator->second.state;
        for (const auto dependency : iterator->second.service->dependencies())
            result.dependencies.emplace_back(dependency);
        return result;
    }

    std::vector<Service_Status> Service_Manager::snapshot() const
    {
        std::scoped_lock lock(m_mutex);
        std::vector<Service_Status> result;
        result.reserve(m_services.size());

        for (const auto& [name, entry] : m_services)
        {
            Service_Status status_value;
            status_value.name = name;
            status_value.state = entry.state;
            for (const auto dependency : entry.service->dependencies())
                status_value.dependencies.emplace_back(dependency);
            result.push_back(std::move(status_value));
        }

        std::ranges::sort(result, {}, &Service_Status::name);
        return result;
    }

    std::size_t Service_Manager::size() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_services.size();
    }

    bool Service_Manager::contains(const std::string_view name) const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_services.contains(std::string(name));
    }

    bool Service_Manager::build_start_order(std::vector<std::string>& order) const
    {
        std::unordered_map<std::string, std::uint8_t> marks;
        marks.reserve(m_services.size());
        order.clear();
        order.reserve(m_services.size());

        for (const auto& [name, _] : m_services)
        {
            if (!visit(name, marks, order))
                return false;
        }

        return true;
    }

    bool Service_Manager::visit(
        const std::string& name,
        std::unordered_map<std::string, std::uint8_t>& marks,
        std::vector<std::string>& order) const
    {
        const auto mark = marks[name];
        if (mark == 2)
            return true;
        if (mark == 1)
        {
            if (m_logging != nullptr)
                m_logging->error("services", "Circular service dependency detected.", {{"service", name}});
            return false;
        }

        const auto service_iterator = m_services.find(name);
        if (service_iterator == m_services.end())
            return false;

        marks[name] = 1;
        for (const auto dependency_view : service_iterator->second.service->dependencies())
        {
            const std::string dependency(dependency_view);
            if (!m_services.contains(dependency))
            {
                if (m_logging != nullptr)
                    m_logging->error("services", "Missing service dependency.", {
                        {"service", name},
                        {"dependency", dependency}
                    });
                return false;
            }

            if (!visit(dependency, marks, order))
                return false;
        }

        marks[name] = 2;
        order.push_back(name);
        return true;
    }

    void Service_Manager::rollback_initialization(const std::vector<std::string>& initialized) noexcept
    {
        for (auto iterator = initialized.rbegin(); iterator != initialized.rend(); ++iterator)
        {
            auto& entry = m_services.at(*iterator);
            try
            {
                entry.service->shutdown();
            }
            catch (...)
            {
            }
            entry.state = Service_State::registered;
        }
    }

    void Service_Manager::rollback_start(const std::vector<std::string>& started) noexcept
    {
        for (auto iterator = started.rbegin(); iterator != started.rend(); ++iterator)
        {
            auto& entry = m_services.at(*iterator);
            try
            {
                entry.service->stop();
            }
            catch (...)
            {
            }
            entry.state = Service_State::initialized;
        }
    }
}
