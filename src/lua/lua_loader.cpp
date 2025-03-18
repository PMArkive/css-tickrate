#include "lua_loader.hpp"
#include "utl.hpp"

bool LuaScriptLoader::init(const std::filesystem::path &autorun_dir) noexcept
{
    std::scoped_lock lock{m_mutex};

    m_autorun_dir = autorun_dir;

    return true;
}

void LuaScriptLoader::on_load() noexcept
{
    std::scoped_lock lock{m_mutex};

    // Run autorun scripts on plugin load.
    reset_scripts();

    for (auto &&state : m_states)
    {
        state->on_load();
    }
}

void LuaScriptLoader::on_game_frame(bool simulating) noexcept
{
    std::scoped_lock lock{m_mutex};

    // Destruct any states that are pending deletion.
    for (auto &&state_to_delete : m_states_to_delete)
    {
        m_states.erase(
            std::remove_if(
                m_states.begin(), m_states.end(), [&state_to_delete](auto &&state) noexcept { return state->lua().lua_state() == state_to_delete; }),
            m_states.end());
    }

    m_states_to_delete.clear();

    for (auto &&state : m_states)
    {
        state->on_game_frame(simulating);
    }
}

void LuaScriptLoader::reset_scripts() noexcept
{
    std::scoped_lock lock{m_mutex};

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
