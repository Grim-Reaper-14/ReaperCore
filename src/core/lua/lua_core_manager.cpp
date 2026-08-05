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
            if (m_initialized)
                return true;
        }

        if (!files.create_directory(context.scripts_directory)
            || !files.create_directory(context.modules_directory)
            || !files.create_directory(context.data_directory))
        {
            logging.error("lua", "LuaCore_Manager could not create its folders.");
            return false;
        }

        {
            std::scoped_lock lock(m_mutex);
            m_files = &files;
            m_logging = &logging;
            m_context = std::move(context);
            m_initialized = true;
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
        Logging_Manager* logging{};
        std::shared_ptr<Lua_Runtime_Interface> runtime;

        {
            std::scoped_lock runtime_lock(m_runtime_mutex);
            {
                std::scoped_lock lock(m_mutex);
                if (!m_initialized)
                    return;

                logging = m_logging;
                runtime = std::move(m_runtime);
                for (auto& script : m_scripts)
                    script.state = Lua_Script_State::discovered;

                m_files = nullptr;
                m_logging = nullptr;
                m_initialized = false;
            }

            if (runtime)
                runtime->shutdown();
        }

        if (logging)
            logging->info("lua", "LuaCore_Manager stopped.");
    }

    bool LuaCore_Manager::install_runtime(std::unique_ptr<Lua_Runtime_Interface> runtime)
    {
        if (!runtime)
            return false;

        Lua_Runtime_Context context;
        Logging_Manager* logging{};
        {
            std::scoped_lock lock(m_mutex);
            if (!m_initialized)
                return false;
            context = m_context;
            logging = m_logging;
        }

        auto incoming = std::shared_ptr<Lua_Runtime_Interface>(std::move(runtime));
        std::string error;
        if (!incoming->initialize(context, error))
        {
            if (logging)
            {
                logging->error("lua", "Lua runtime initialization failed.", {
                    {"error", error}
                });
            }
            return false;
        }

        std::shared_ptr<Lua_Runtime_Interface> previous;
        {
            std::scoped_lock runtime_lock(m_runtime_mutex);
            {
                std::scoped_lock lock(m_mutex);
                if (!m_initialized)
                {
                    incoming->shutdown();
                    return false;
                }
                previous = std::move(m_runtime);
                m_runtime = incoming;
            }

            if (previous)
                previous->shutdown();
        }

        if (logging)
            logging->info("lua", "Lua runtime installed.");
        return true;
    }

    void LuaCore_Manager::remove_runtime() noexcept
    {
        Logging_Manager* logging{};
        std::shared_ptr<Lua_Runtime_Interface> runtime;

        {
            std::scoped_lock runtime_lock(m_runtime_mutex);
            {
                std::scoped_lock lock(m_mutex);
                logging = m_logging;
                runtime = std::move(m_runtime);
                for (auto& script : m_scripts)
                    script.state = Lua_Script_State::discovered;
            }

            if (runtime)
                runtime->shutdown();
        }

        if (logging && runtime)
            logging->info("lua", "Lua runtime removed.");
    }

    bool LuaCore_Manager::runtime_ready() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_runtime != nullptr;
    }

    std::size_t LuaCore_Manager::discover_scripts()
    {
        File_System_Manager* files{};
        Logging_Manager* logging{};
        std::filesystem::path scripts_directory;
        {
            std::scoped_lock lock(m_mutex);
            if (!m_initialized || m_files == nullptr)
                return 0;
            files = m_files;
            logging = m_logging;
            scripts_directory = m_context.scripts_directory;
        }

        const auto paths = files->list_files(scripts_directory, ".lua", true);
        std::vector<Lua_Script_Entry> discovered;
        discovered.reserve(paths.size());
        for (const auto& path : paths)
        {
            Lua_Script_Entry entry;
            entry.id = make_id(path, scripts_directory);
            entry.name = path.stem().string();
            entry.path = path;
            discovered.push_back(std::move(entry));
        }

        {
            std::scoped_lock lock(m_mutex);
            for (auto& entry : discovered)
            {
                const auto existing = std::find_if(
                    m_scripts.begin(),
                    m_scripts.end(),
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

        if (logging)
        {
            logging->info("lua", "Lua script discovery completed.", {
                {"count", std::to_string(paths.size())}
            });
        }
        return paths.size();
    }

    bool LuaCore_Manager::load_script(const std::string_view id)
    {
        Logging_Manager* logging{};
        std::shared_ptr<Lua_Runtime_Interface> runtime;
        Lua_Script_Entry script_snapshot;

        {
            std::scoped_lock runtime_lock(m_runtime_mutex);
            {
                std::scoped_lock lock(m_mutex);
                const auto index = find_index_unlocked(id);
                if (!index)
                    return false;

                auto& script = m_scripts[*index];
                logging = m_logging;
                runtime = m_runtime;
                if (!runtime)
                {
                    script.state = Lua_Script_State::faulted;
                    script.last_error = "No Lua runtime is installed.";
                    script_snapshot = script;
                }
                else
                {
                    script_snapshot = script;
                }
            }

            if (!runtime)
            {
                if (logging)
                {
                    logging->warning(
                        "lua",
                        "Script load deferred because no runtime is installed.",
                        {{"script", script_snapshot.id}});
                }
                return false;
            }

            std::string error;
            bool loaded{};
            try
            {
                loaded = runtime->load_script(script_snapshot, error);
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
            }
            catch (...)
            {
                error = "Unknown exception while loading the script.";
            }

            {
                std::scoped_lock lock(m_mutex);
                const auto current = find_index_unlocked(id);
                if (!current)
                    return false;

                auto& script = m_scripts[*current];
                if (loaded)
                {
                    script.state = Lua_Script_State::loaded;
                    script.last_error.clear();
                }
                else
                {
                    script.state = Lua_Script_State::faulted;
                    script.last_error = error.empty()
                        ? "Lua runtime rejected the script."
                        : std::move(error);
                }
                script_snapshot = script;
            }
        }

        if (script_snapshot.state == Lua_Script_State::loaded)
        {
            if (logging)
                logging->info("lua", "Lua script loaded.", {{"script", script_snapshot.id}});
            return true;
        }

        if (logging)
        {
            logging->error("lua", "Lua script failed to load.", {
                {"script", script_snapshot.id},
                {"error", script_snapshot.last_error}
            });
        }
        return false;
    }

    bool LuaCore_Manager::unload_script(const std::string_view id)
    {
        Logging_Manager* logging{};
        std::shared_ptr<Lua_Runtime_Interface> runtime;
        Lua_Script_Entry script_snapshot;
        bool unloaded{true};

        {
            std::scoped_lock runtime_lock(m_runtime_mutex);
            {
                std::scoped_lock lock(m_mutex);
                const auto index = find_index_unlocked(id);
                if (!index)
                    return false;

                auto& script = m_scripts[*index];
                logging = m_logging;
                runtime = m_runtime;
                script_snapshot = script;

                if (!runtime || script.state != Lua_Script_State::loaded)
                {
                    script.state = Lua_Script_State::discovered;
                    script.last_error.clear();
                    return true;
                }
            }

            std::string error;
            try
            {
                unloaded = runtime->unload_script(script_snapshot, error);
            }
            catch (const std::exception& exception)
            {
                unloaded = false;
                error = exception.what();
            }
            catch (...)
            {
                unloaded = false;
                error = "Unknown exception while unloading the script.";
            }

            {
                std::scoped_lock lock(m_mutex);
                const auto current = find_index_unlocked(id);
                if (!current)
                    return false;

                auto& script = m_scripts[*current];
                if (unloaded)
                {
                    script.state = Lua_Script_State::discovered;
                    script.last_error.clear();
                }
                else
                {
                    script.state = Lua_Script_State::faulted;
                    script.last_error = error.empty()
                        ? "Lua runtime rejected the unload request."
                        : std::move(error);
                }
                script_snapshot = script;
            }
        }

        if (unloaded)
        {
            if (logging)
                logging->info("lua", "Lua script unloaded.", {{"script", script_snapshot.id}});
            return true;
        }

        if (logging)
        {
            logging->error("lua", "Lua script failed to unload.", {
                {"script", script_snapshot.id},
                {"error", script_snapshot.last_error}
            });
        }
        return false;
    }

    bool LuaCore_Manager::reload_script(const std::string_view id)
    {
        if (!unload_script(id))
            return false;
        return load_script(id);
    }

    void LuaCore_Manager::tick()
    {
        Logging_Manager* logging{};
        std::string failure;

        {
            std::scoped_lock runtime_lock(m_runtime_mutex);
            std::shared_ptr<Lua_Runtime_Interface> runtime;
            {
                std::scoped_lock lock(m_mutex);
                runtime = m_runtime;
                logging = m_logging;
            }

            if (!runtime)
                return;

            try
            {
                runtime->tick();
            }
            catch (const std::exception& exception)
            {
                failure = exception.what();
            }
            catch (...)
            {
                failure = "Unknown exception during Lua runtime tick.";
            }
        }

        if (logging && !failure.empty())
        {
            logging->error("lua", "Lua runtime tick failed.", {
                {"error", failure}
            });
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
        return index
            ? std::optional<Lua_Script_Entry>(m_scripts[*index])
            : std::nullopt;
    }

    std::optional<std::size_t> LuaCore_Manager::find_index_unlocked(
        const std::string_view id) const
    {
        const auto found = std::find_if(
            m_scripts.begin(),
            m_scripts.end(),
            [id](const Lua_Script_Entry& script)
            {
                return script.id == id;
            });

        if (found == m_scripts.end())
            return std::nullopt;
        return static_cast<std::size_t>(std::distance(m_scripts.begin(), found));
    }

    std::string LuaCore_Manager::make_id(
        const std::filesystem::path& path,
        const std::filesystem::path& scripts_directory) const
    {
        std::error_code error;
        const auto relative = std::filesystem::relative(path, scripts_directory, error);
        return error
            ? path.filename().generic_string()
            : relative.generic_string();
    }
}
