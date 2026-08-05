#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reapercore
{
    class File_System_Manager;
    class Logging_Manager;

    enum class Lua_Script_State
    {
        discovered,
        loaded,
        faulted
    };

    struct Lua_Script_Entry
    {
        std::string id;
        std::string name;
        std::filesystem::path path;
        Lua_Script_State state{Lua_Script_State::discovered};
        bool enabled{true};
        bool auto_load{false};
        std::string last_error;
    };

    struct Lua_Runtime_Context
    {
        std::filesystem::path scripts_directory;
        std::filesystem::path modules_directory;
        std::filesystem::path data_directory;
    };

    class Lua_Runtime_Interface
    {
    public:
        virtual ~Lua_Runtime_Interface() = default;

        virtual bool initialize(
            const Lua_Runtime_Context& context,
            std::string& error) = 0;
        virtual void shutdown() noexcept = 0;
        virtual bool load_script(
            const Lua_Script_Entry& script,
            std::string& error) = 0;
        virtual bool unload_script(
            const Lua_Script_Entry& script,
            std::string& error) = 0;
        virtual void tick() = 0;
    };

    class LuaCore_Manager final
    {
    public:
        bool initialize(
            File_System_Manager& files,
            Logging_Manager& logging,
            Lua_Runtime_Context context);
        void shutdown() noexcept;

        bool install_runtime(std::unique_ptr<Lua_Runtime_Interface> runtime);
        void remove_runtime() noexcept;
        [[nodiscard]] bool runtime_ready() const noexcept;

        [[nodiscard]] std::size_t discover_scripts();
        [[nodiscard]] bool load_script(std::string_view id);
        [[nodiscard]] bool unload_script(std::string_view id);
        [[nodiscard]] bool reload_script(std::string_view id);
        void tick();

        [[nodiscard]] std::vector<Lua_Script_Entry> scripts() const;
        [[nodiscard]] std::optional<Lua_Script_Entry> script(
            std::string_view id) const;

    private:
        [[nodiscard]] std::optional<std::size_t> find_index_unlocked(
            std::string_view id) const;
        [[nodiscard]] std::string make_id(
            const std::filesystem::path& path,
            const std::filesystem::path& scripts_directory) const;

        File_System_Manager* m_files{};
        Logging_Manager* m_logging{};
        Lua_Runtime_Context m_context;
        std::shared_ptr<Lua_Runtime_Interface> m_runtime;
        mutable std::mutex m_mutex;
        mutable std::mutex m_runtime_mutex;
        std::vector<Lua_Script_Entry> m_scripts;
        bool m_initialized{};
    };
}
