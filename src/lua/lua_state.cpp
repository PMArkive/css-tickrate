#include "lua_state.hpp"
#include "common.hpp"
#include "utl.hpp"
#include <algorithm>

LuaScriptState::LuaScriptState(bool is_main_state) noexcept : m_is_main_state{is_main_state}
{
    std::scoped_lock lock{m_lua_mutex};

    m_lua.registry()["tr_state"] = this;

    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::coroutine,
        sol::lib::string,
        sol::lib::os,
        sol::lib::math,
        sol::lib::table,
        sol::lib::debug,
        sol::lib::bit32,
        sol::lib::io,
        sol::lib::utf8);

    // Restrict the Lua OS library.
    auto &&os       = m_lua["os"];
    os["execute"]   = sol::nil;
    os["exit"]      = sol::nil;
    os["getenv"]    = sol::nil;
    os["remove"]    = sol::nil;
    os["rename"]    = sol::nil;
    os["setlocale"] = sol::nil;

    auto tr      = m_lua.create_table();
    tr["square"] = [](f64 x) noexcept
    {
        return x * x;
    };
    tr["add_callback"] = [this](const std::string &name, const sol::object &fn) noexcept
    {
        std::scoped_lock lock{m_lua_mutex};

        if (!fn.is<sol::protected_function>())
        {
            return false;
        }

        auto found_id = str_to_callback_id(name);
        if (!found_id)
        {
            return false;
        }

        auto &&cbs = m_callbacks[*found_id];

        // Already exists? Don't add.
        if (std::find(cbs.begin(), cbs.end(), fn) != cbs.end())
        {
            return false;
        }

        cbs.emplace_back(fn);

        return true;
    };

    m_lua["tr"] = tr;
}

std::optional<LuaScriptState::CallbackID> LuaScriptState::str_to_callback_id(const std::string &name) const noexcept
{
    auto found = m_callback_names.find(name);
    if (found == m_callback_names.end())
    {
        return {};
    }

    return found->second;
}

tl::expected<void, std::string> LuaScriptState::run_script_file(const std::filesystem::path &full_path) noexcept
{
    std::scoped_lock lock{m_lua_mutex};

    // Only allow requiring from the directory the script was run from.
    auto &&old_path  = m_lua["package"]["path"];
    auto &&old_cpath = m_lua["package"]["cpath"];

    auto dir = full_path.parent_path().string();

    // Just make it consistent. This should be fine on Windows.
#if TR_OS_WINDOWS
    std::replace(dir.begin(), dir.end(), '\\', '/');
#endif

    std::string new_path  = dir + "/?.lua";
    new_path             += ';' + dir + "/?/init.lua";

    std::string new_cpath; // TODO: No .dll/.so files for now.

    m_lua["package"]["path"]  = new_path;
    m_lua["package"]["cpath"] = new_cpath;

    auto result = m_lua.safe_script_file(full_path.string());

    m_lua["package"]["path"]  = old_path;
    m_lua["package"]["cpath"] = old_cpath;

    if (!result.valid())
    {
        sol::error err = result;
        return tl::unexpected{err.what()};
    }

    return {};
}

void LuaScriptState::on_load() noexcept
{
    std::scoped_lock lock{m_lua_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_load])
    {
        if (auto result = cb(); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] on_load error: {}\n", err.what());
        }
    }
}

void LuaScriptState::on_script_reset() noexcept
{
    std::scoped_lock lock{m_lua_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_script_reset])
    {
        if (auto result = cb(); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] on_script_reset error: {}\n", err.what());
        }
    }
}

void LuaScriptState::on_game_frame(bool simulating) noexcept
{
    std::scoped_lock lock{m_lua_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_game_frame])
    {
        if (auto result = cb(simulating); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] on_game_frame error: {}\n", err.what());
        }
    }

    // TODO: Handle garbage collection if needed.
}
