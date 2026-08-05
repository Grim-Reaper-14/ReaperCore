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

    struct Module_Section
    {
        std::string name;
        std::uintptr_t address{};
        std::size_t virtual_size{};
        std::size_t raw_size{};
        std::uint32_t characteristics{};

        [[nodiscard]] bool executable() const noexcept;
        [[nodiscard]] bool readable() const noexcept;
        [[nodiscard]] bool writable() const noexcept;
    };

    struct Module_Export
    {
        std::string name;
        std::uint32_t ordinal{};
        std::uintptr_t address{};
        bool forwarded{};
        std::string forward_target;
    };

    struct Module_Import
    {
        std::string source_module;
        std::string symbol;
        std::uint16_t ordinal{};
        std::uintptr_t thunk_address{};
        bool imported_by_ordinal{};
    };

    struct Module_Integrity
    {
        std::uint64_t image_hash{};
        std::uint64_t executable_sections_hash{};
        std::uint64_t generation{};
        bool changed{};
    };

    struct Module_Record
    {
        std::string name;
        std::filesystem::path path;
        std::uintptr_t base_address{};
        std::uintptr_t entry_point{};
        std::size_t image_size{};
        std::uint32_t timestamp{};
        std::uint16_t machine{};
        std::uint16_t subsystem{};
        std::uint16_t section_count{};
        bool is_64_bit{};
        bool is_system_module{};
        std::string file_version;
        std::string product_version;
        std::vector<Module_Section> sections;
        std::vector<Module_Export> exports;
        std::vector<Module_Import> imports;
        Module_Integrity integrity;

        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] bool contains(std::uintptr_t address) const noexcept;
        [[nodiscard]] std::optional<Module_Section> find_section(std::string_view section_name) const;
        [[nodiscard]] std::optional<Module_Export> find_export(std::string_view export_name) const;
    };

    class Module_Catalog final
    {
    public:
        bool initialize(Logging_Manager& logging) noexcept;
        bool refresh_current_process();
        bool refresh_module(std::string_view name);
        bool capture_integrity_baseline();
        bool verify_integrity();
        void clear() noexcept;
        void shutdown() noexcept;

        [[nodiscard]] std::optional<Module_Record> find(std::string_view name) const;
        [[nodiscard]] std::optional<Module_Record> find_by_address(std::uintptr_t address) const;
        [[nodiscard]] std::vector<Module_Record> snapshot() const;
        [[nodiscard]] std::vector<Module_Record> changed_modules() const;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] std::uint64_t generation() const noexcept;
        [[nodiscard]] bool initialized() const noexcept;

    private:
        static std::string normalize_name(std::string_view name);
        static std::optional<Module_Record> inspect_module(
            std::uintptr_t base_address,
            const std::filesystem::path& path,
            std::uint64_t generation);

        Logging_Manager* m_logging{};
        mutable std::mutex m_mutex;
        std::unordered_map<std::string, Module_Record> m_modules;
        std::uint64_t m_generation{};
        bool m_initialized{};
    };
}
