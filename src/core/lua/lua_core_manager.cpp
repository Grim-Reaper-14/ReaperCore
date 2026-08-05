#include "reapercore/core/lua/lua_core_manager.hpp"
#include "reapercore/core/file_system/file_system_manager.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace reapercore
{
    bool LuaCore_Manager::initialize(
        File_System_Manager& files,
        Logging_Manager& logging,
        Lua_Runtime_Context context)
    {
        {
            std::scoped_lock lock(m_mutex);
            m_files = &files;
            m_logging = &logging;
            m_context = std::move(context);
            m_initialized = true;
        }

        if (!files.create_directory(m_context.scripts_directory)
            || !files.create_directory(m_context.modules_directory)
            || !files.create_directory(m_context.data_directory))
        {
            logging.error("lua", "LuaCore_Manager could not create its folders.");
            return false;
        }

        logging.info("lua", "LuaCore_Manager initialized.", {
            {"scripts", m_context.scripts_directory.string()},
            {"modules", m_context.modules_directory.string()},
            {"data", m_context.data_directory.string()}
        });
        return true;
    }

    void LuaCore_Manager::shutdown() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (!m_initialized)
            return;

        if (m_runtime)
            m_runtime->shutdown();
        m_runtime.reset();
        for (auto& script : m_scripts)
            script.state = Lua_Script_State::discovered;
        m_initialized = false;
    }

    bool LuaCore_Manager::install_runtime(std::unique_ptr<Lua_Runtime_Interface> runtime)
    {
        if (!runtime)
            return false;

        std::scoped_lock lock(m_mutex);
        if (!m_initialized)
            return false;

        if (m_runtime)
            m_runtime->shutdown();

        std::string error;
        if (!runtime->initialize(m_context, error))
        {
            if (m_logging)
                m_logging->error("lua", "Lua runtime initialization failed.", {
                    {"error", error}
                });
            return false;
        }

        m_runtime = std::move(runtime);
        if (m_logging)
            m_logging->info("lua", "Lua runtime installed.");
        return true;
    }

    void LuaCore_Manager::remove_runtime() noexcept
    {
        std::scoped_lock lock(m_mutex);
        if (m_runtime)
            m_runtime->shutdown();
        m_runtime.reset();
        for (auto& script : m_scripts)
            script.state = Lua_Script_State::discovered;
    }

    bool LuaCore_Manager::runtime_ready() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_runtime != nullptr;
    }

    std::size_t LuaCore_Manager::discover_scripts()
    {
        File_System_Manager* files{};
        std::filesystem::path scripts_directory;
        {
            std::scoped_lock lock(m_mutex);
            if (!m_initialized || m_files == nullptr)
                return 0;
            files = m_files;
            scripts_directory = m_context.scripts_directory;
        }

        const auto paths = files->list_files(scripts_directory, ".lua", true);
        std::vector<Lua_Script_Entry> discovered;
        discovered.reserve(paths.size());
        for (const auto& path : paths)
        {
            Lua_Script_Entry entry;
            entry.id = make_id(path);
            entry.name = path.stem().string();
            entry.path = path;
            discovered.push_back(std::move(entry));
        }

        {
            std::scoped_lock lock(m_mutex);
            for (auto& entry : discovered)
            {
                const auto existing = std::find_if(m_scripts.begin(), m_scripts.end(),
                    [&entry](const Lua_Script_Entry& value)
                    {
                        return value.id == entry.id;
                    });
                if (existing != m_scripts.end())
                {
                    entry.state = existing->state;
                    entry.enabled = existing->enabled;
                    entry.auto_load = existing->auto_load;
                    entry.last_error = existing->last_error;
                }
            }
            m_scripts = std::move(discovered);
        }

        if (m_logging)
            m_logging->info("lua", "Lua script discovery completed.", {
                {"count", std::to_string(paths.size())}
            });
        return paths.size();
    }

    bool LuaCore_Manager::load_script(const std::string_view id)
    {
        std::scoped_lock lock(m_mutex);
        const auto index = find_index_unlocked(id);
        if (!index)
            return false;
        auto& script = m_scripts[*index];

        if (!m_runtime)
        {
            script.state = Lua_Script_State::faulted;
            script.last_error = "No Lua runtime is installed.";
            if (m_logging)
                m_logging->warning("lua", "Script load deferred because no runtime is installed.", {
                    {"script", script.id}
                });
            return false;
        }

        std::string error;
        if (!m_runtime->load_script(script, error))
        {
            script.state = Lua_Script_State::faulted;
            script.last_error = std::move(error);
            if (m_logging)
                m_logging->error("lua", "Lua script failed to load.", {
                    {"script", script.id},
                    {"error", script.last_error}
                });
            return false;
        }

        script.state = Lua_Script_State::loaded;
        script.last_error.clear();
        if (m_logging)
            m_logging->info("lua", "Lua script loaded.", {{"script", script.id}});
        return true;
    }

    bool LuaCore_Manager::unload_script(const std::string_view id)
    {
        std::scoped_lock lock(m_mutex);
        const auto index = find_index_unlocked(id);
        if (!index)
            return false;
        auto& script = m_scripts[*index];

        if (!m_runtime || script.state != Lua_Script_State::loaded)
        {
            script.state = Lua_Script_State::discovered;
            return true;
        }

        std::string error;
        if (!m_runtime->unload_script(script, error))
        {
            script.state = Lua_Script_State::faulted;
            script.last_error = std::move(error);
            if (m_logging)
                m_logging->error("lua", "Lua script failed to unload.", {
                    {"script", script.id},
                    {"error", script.last_error}
                });
            return false;
        }

        script.state = Lua_Script_State::discovered;
        script.last_error.clear();
        if (m_logging)
            m_logging->info("lua", "Lua script unloaded.", {{"script", script.id}});
        return true;
    }

    bool LuaCore_Manager::reload_script(const std::string_view id)
    {
        static_cast<void>(unload_script(id));
        return load_script(id);
    }

    void LuaCore_Manager::tick()
    {
        std::scoped_lock lock(m_mutex);
        if (!m_runtime)
            return;

        try
        {
            m_runtime->tick();
        }
        catch (const std::exception& exception)
        {
            if (m_logging)
                m_logging->log_exception("lua", exception, "Lua runtime tick failed.");
        }
        catch (...)
        {
            if (m_logging)
                m_logging->critical("lua", "Lua runtime tick failed with an unknown exception.");
        }
    }

    std::vector<Lua_Script_Entry> LuaCore_Manager::scripts() const
    {
        std::scoped_lock lock(m_mutex);
        return m_scripts;
    }

    std::optional<Lua_Script_Entry> LuaCore_Manager::script(
        const std::string_view id) const
    {
        std::scoped_lock lock(m_mutex);
        const auto index = find_index_unlocked(id);
        return index ? std::optional<Lua_Script_Entry>(m_scripts[*index]) : std::nullopt;
    }

    std::optional<std::size_t> LuaCore_Manager::find_index_unlocked(
        const std::string_view id) const
    {
        const auto found = std::find_if(m_scripts.begin(), m_scripts.end(),
            [id](const Lua_Script_Entry& script)
            {
                return script.id == id;
            });
        if (found == m_scripts.end())
            return std::nullopt;
        return static_cast<std::size_t>(std::distance(m_scripts.begin(), found));
    }

    std::string LuaCore_Manager::make_id(const std::filesystem::path& path) const
    {
        std::error_code error;
        const auto relative = std::filesystem::relative(
            path, m_context.scripts_directory, error);
        return error ? path.filename().generic_string() : relative.generic_string();
    }
}
