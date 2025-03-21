#include "lua_loader.hpp"
#include "utl.hpp"

bool LuaScriptLoader::init(const std::filesystem::path &autorun_dir) noexcept
{
    std::scoped_lock _{m_lua_mutex};

    m_autorun_dir = autorun_dir;

    return true;
}

void LuaScriptLoader::lock() noexcept
{
    m_lua_mutex.lock();

    for (auto &&state : m_states)
    {
        state->lock();
    }

    ++m_lock_depth;
}

void LuaScriptLoader::unlock() noexcept
{
    for (auto &&state : m_states)
    {
        state->unlock();
    }

    m_lua_mutex.unlock();

    if (m_lock_depth > 0)
    {
        --m_lock_depth;
    }
}

lua_State *LuaScriptLoader::create_state() noexcept
{
    std::scoped_lock _{m_lua_mutex};

    auto &&result = m_states.emplace_back(std::make_shared<LuaScriptState>(false));

    for (usize i{}; i < m_lock_depth; ++i)
    {
        m_states.back()->lock();
    }

    return result->lua().lua_state();
}

void LuaScriptLoader::delete_state(lua_State *state) noexcept
{
    std::scoped_lock _{m_lua_mutex};

    // Don't allow deletion of main state.
    if (state == m_main_state->lua().lua_state())
    {
        return;
    }

    auto found = std::find_if(m_states.begin(), m_states.end(), [state](auto &&s) noexcept { return state == s->lua().lua_state(); });
    if (found == m_states.end())
    {
        return;
    }

    m_states_to_delete.emplace_back(state);
}

void LuaScriptLoader::reset_scripts() noexcept
{
    std::scoped_lock _{m_lua_mutex};

    // Only call the `on_script_reset` callbacks on the main state.
    if (m_main_state != nullptr)
    {
        m_main_state->on_script_reset();
    }

    // Set up states.
    m_main_state = {};
    m_states.clear();

    m_main_state = std::make_shared<LuaScriptState>(true);
    m_states.insert(m_states.begin(), m_main_state);

    // Run autorun scripts.
    std::error_code ec;
    for (std::filesystem::directory_iterator it{m_autorun_dir, ec}, empty; it != empty && ec == std::error_code{}; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }

        auto &&path = it->path();
        if (path.extension() != ".lua")
        {
            continue;
        }

        utl::print_info("[LuaScriptLoader] Running autorun script: `{}`.\n", path.filename().string());

        // TODO: Save script paths.

        auto run_result = m_main_state->run_script_file(path);
        if (run_result)
        {
            utl::print_info("[LuaScriptLoader] Ran autorun script: `{}`.\n", path.filename().string());
        }
        else
        {
            utl::print_error("[LuaScriptLoader] Failed to load autorun script: `{}`.\n{}\n", path.filename().string(), run_result.error());
        }
    }
}

void LuaScriptLoader::on_load() noexcept
{
    std::scoped_lock _{m_lua_mutex};

    // Run autorun scripts on plugin load.
    reset_scripts();

    for (auto &&state : m_states)
    {
        state->on_load();
    }
}

void LuaScriptLoader::on_game_frame(bool simulating) noexcept
{
    std::scoped_lock _{m_lua_mutex};

    // Destruct any states that are pending deletion.
    for (auto &&s : m_states_to_delete)
    {
        m_states.erase(
            std::remove_if(m_states.begin(), m_states.end(), [s](auto &&state) noexcept { return state->lua().lua_state() == s; }), m_states.end());
    }

    m_states_to_delete.clear();

    for (auto &&state : m_states)
    {
        state->on_game_frame(simulating);
    }
}

void LuaScriptLoader::on_level_init(std::string_view map_name) noexcept
{
    std::scoped_lock _{m_lua_mutex};

    for (auto &&state : m_states)
    {
        state->on_level_init(map_name);
    }
}

void LuaScriptLoader::on_level_shutdown() noexcept
{
    std::scoped_lock _{m_lua_mutex};

    for (auto &&state : m_states)
    {
        state->on_level_shutdown();
    }
}
