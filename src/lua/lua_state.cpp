#include "lua_state.hpp"
#include "common.hpp"
#include "utl.hpp"
#include "game_data.hpp"
#include <algorithm>

LuaScriptState::LuaScriptState(bool is_main_state) noexcept : m_is_main_state{is_main_state}
{
    std::scoped_lock lock{m_exec_mutex};

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

    auto tr = m_lua.create_table();

    tr.new_usertype<GameData>("GameData", "new", sol::no_constructor, "get_mod_name", [](GameData &self) noexcept { return self.mod_name; });
    tr["gamedata"] = &g_game_data;

    tr["square"] = [](f64 x) noexcept
    {
        return x * x;
    };
    tr["add_callback"] = [this](const std::string &name, const sol::object &fn) noexcept
    {
        std::scoped_lock lock{m_exec_mutex};

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

        // Don't add another hook if the function is the same.
        if (std::find(cbs.begin(), cbs.end(), fn) != cbs.end())
        {
            return false;
        }

        cbs.emplace_back(fn);

        return true;
    };

    m_lua["tr"] = tr;
}

LuaScriptState::~LuaScriptState() noexcept
{
    std::scoped_lock lock{m_exec_mutex};
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
    std::scoped_lock lock{m_exec_mutex};

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

void LuaScriptState::on_script_reset() noexcept
{
    std::scoped_lock lock{m_exec_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_script_reset])
    {
        if (auto result = cb(); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] on_script_reset error: {}\n", err.what());
        }
    }
}

void LuaScriptState::on_load() noexcept
{
    std::scoped_lock lock{m_exec_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_load])
    {
        if (auto result = cb(); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] on_load error: {}\n", err.what());
        }
    }
}

void LuaScriptState::on_level_init(std::string_view map_name) noexcept
{
    std::scoped_lock lock{m_exec_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_level_init])
    {
        if (auto result = cb(map_name); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] on_level_init error: {}\n", err.what());
        }
    }
}

void LuaScriptState::on_level_shutdown() noexcept
{
    std::scoped_lock lock{m_exec_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_level_shutdown])
    {
        if (auto result = cb(); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] on_level_shutdown error: {}\n", err.what());
        }
    }
}

void LuaScriptState::on_game_frame(bool simulating) noexcept
{
    std::scoped_lock lock{m_exec_mutex};

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
