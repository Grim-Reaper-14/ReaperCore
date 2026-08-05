#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reapercore
{
    class Logging_Manager;

    struct Module_Record
    {
        std::string name;
        std::filesystem::path path;
        std::uintptr_t base_address{};
        std::size_t image_size{};

        [[nodiscard]] bool valid() const noexcept
        {
            return base_address != 0 && image_size != 0;
        }
    };

    class Module_Catalog final
    {
    public:
        bool initialize(Logging_Manager& logging) noexcept;
        bool refresh_current_process();
        void clear() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] std::optional<Module_Record> find(std::string_view name) const;
        [[nodiscard]] std::vector<Module_Record> snapshot() const;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool initialized() const noexcept;

    private:
        static std::string normalize_name(std::string_view name);

        Logging_Manager* m_logging{};
        mutable std::mutex m_mutex;
        std::unordered_map<std::string, Module_Record> m_modules;
        bool m_initialized{};
    };
}
