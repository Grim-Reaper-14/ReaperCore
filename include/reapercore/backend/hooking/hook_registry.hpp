#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace reapercore
{
    class Logging_Manager;

    struct Hook_Descriptor
    {
        std::string name;
        std::function<bool()> install;
        std::function<void()> uninstall;
        bool installed{};
    };

    class Hook_Registry final
    {
    public:
        bool initialize(Logging_Manager& logging) noexcept;
        bool register_hook(Hook_Descriptor descriptor);
        bool install_all();
        void uninstall_all() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] std::size_t registered_count() const noexcept;
        [[nodiscard]] std::size_t installed_count() const noexcept;
        [[nodiscard]] bool contains(std::string_view name) const;

    private:
        Logging_Manager* m_logging{};
        mutable std::mutex m_mutex;
        std::vector<Hook_Descriptor> m_hooks;
        bool m_initialized{};
    };
}
