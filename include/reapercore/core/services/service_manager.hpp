#pragma once

#include "reapercore/core/services/service.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reapercore
{
    class Logging_Manager;

    struct Service_Status
    {
        std::string name;
        Service_State state{Service_State::registered};
        std::vector<std::string> dependencies;
    };

    class Service_Manager final
    {
    public:
        explicit Service_Manager(Logging_Manager& logging) noexcept;
        ~Service_Manager();

        Service_Manager(const Service_Manager&) = delete;
        Service_Manager& operator=(const Service_Manager&) = delete;

        bool register_service(std::unique_ptr<Service> service);
        bool initialize_all();
        bool start_all();
        void tick_all();
        void stop_all() noexcept;
        void shutdown_all() noexcept;

        [[nodiscard]] Service* find(std::string_view name) noexcept;
        [[nodiscard]] const Service* find(std::string_view name) const noexcept;
        [[nodiscard]] std::optional<Service_Status> status(std::string_view name) const;
        [[nodiscard]] std::vector<Service_Status> snapshot() const;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool contains(std::string_view name) const noexcept;

    private:
        struct Entry
        {
            std::unique_ptr<Service> service;
            Service_State state{Service_State::registered};
        };

        bool build_start_order(std::vector<std::string>& order) const;
        bool visit(
            const std::string& name,
            std::unordered_map<std::string, std::uint8_t>& marks,
            std::vector<std::string>& order) const;
        void rollback_initialization(const std::vector<std::string>& initialized) noexcept;
        void rollback_start(const std::vector<std::string>& started) noexcept;

        Logging_Manager* m_logging{};
        mutable std::recursive_mutex m_mutex;
        std::unordered_map<std::string, Entry> m_services;
        std::vector<std::string> m_order;
        bool m_initialized{};
        bool m_started{};
    };
}
