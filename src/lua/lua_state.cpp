#include "lua_state.hpp"
#include "common.hpp"
#include "utl.hpp"
#include <cstring>
#include <utility>
#include <algorithm>

void lua_panic_handler(sol::optional<std::string> maybe_msg) noexcept
{
    if (maybe_msg)
    {
        utl::print_error("Lua panic occurred:\n{}", *maybe_msg);
    }

    // When this function exits, Lua will exhibit default behavior and `abort()`.
}

i32 lua_exception_handler(lua_State *L, [[maybe_unused]] sol::optional<const std::exception &> maybe_exception, sol::string_view description) noexcept
{
    // state is the lua state, which you can wrap in a state_view if necessary maybe_exception will contain exception, if it exists description will
    // either be the `what()` of the exception or a description saying that we hit the general-case catch(...).
    utl::print_error("Lua exception occurred:\n{}", description);

    // you must push 1 element onto the stack to be transported through as the error object in Lua note that Lua -- and 99.5% of all Lua users and
    // libraries -- expects a string so we push a single string (in our case, the description of the error).
    return sol::stack::push(L, description);
}

std::string lua_get_file_name(lua_State *L) noexcept
{
    // 0 = this func
    // 1 = lua func
    lua_Debug info;
    if (lua_getstack(L, 1, &info) != 1)
    {
        return "?";
    }

    if (lua_getinfo(L, "Sl", &info) == 0)
    {
        return "?";
    }

    std::string_view source = info.source;
    if (source.front() != '@')
    {
        return "?";
    }

    return fmt::format("{}:{}", std::filesystem::path{source.substr(1)}.filename().string(), info.currentline);
}

template <class... Args>
void lua_print_info(lua_State *L, fmt::format_string<Args...> fmt, Args &&...args) noexcept
{
    utl::print_info("[Lua `{}`] {}", lua_get_file_name(L), fmt::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void lua_print_error(lua_State *L, fmt::format_string<Args...> fmt, Args &&...args) noexcept
{
    utl::print_error("[Lua `{}`] {}", lua_get_file_name(L), fmt::format(fmt, std::forward<Args>(args)...));
}

LuaScriptState::LuaScriptState(bool is_main_state) noexcept : m_is_main_state{is_main_state}
{
    std::scoped_lock lock{m_exec_mutex};

    m_lua.set_panic(sol::c_call<decltype(&lua_panic_handler), &lua_panic_handler>);
    m_lua.set_exception_handler(&lua_exception_handler);
    // sol::protected_function::set_default_handler();

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
    auto os         = m_lua["os"];
    os["execute"]   = sol::nil;
    os["exit"]      = sol::nil;
    os["getenv"]    = sol::nil;
    os["remove"]    = sol::nil;
    os["rename"]    = sol::nil;
    os["setlocale"] = sol::nil;

    // Create the `tr` table.
    auto tr = m_lua.create_named_table("tr");

    tr["print_info"] = [](sol::this_state L, sol::stack_object value) noexcept
    {
        if (value.is<std::string_view>())
        {
            lua_print_info(L, "{}", value.as<std::string_view>());
        }
        else
        {
            auto *str = luaL_tolstring(L, value.stack_index(), nullptr);

            lua_print_info(L, "{}", str);

            // `luaL_tolstring` will push onto the stack.
            lua_pop(L, 1);
        }
    };

    tr["print_error"] = [](sol::this_state L, sol::stack_object value) noexcept
    {
        if (value.is<std::string_view>())
        {
            lua_print_error(L, "{}", value.as<std::string_view>());
        }
        else
        {
            auto *str = luaL_tolstring(L, value.stack_index(), nullptr);

            lua_print_error(L, "{}", str);

            // `luaL_tolstring` will push onto the stack.
            lua_pop(L, 1);
        }
    };

    m_lua["print"] = tr["print_info"];

    tr["add_callback"] = [this](sol::this_state L, const std::string &name, sol::stack_object fn) noexcept
    {
        std::scoped_lock lock{m_exec_mutex};

        if (!fn.is<sol::protected_function>())
        {
            return false;
        }

        auto found_id = str_to_callback_id(name);
        if (!found_id)
        {
            lua_print_error(L, "Tried adding a callback that doesn't exist: `{}`.", name);
            return false;
        }

        auto &cbs = m_callbacks[*found_id];

        // Don't add another hook if the function is the same.
        if (std::find(cbs.begin(), cbs.end(), fn) != cbs.end())
        {
            return false;
        }

        cbs.emplace_back(fn);

        return true;
    };

    // Protect the `tr` table.
    sol::table tr_metatable           = m_lua.create_table_with();
    tr[sol::meta_function::index]     = tr;
    tr[sol::meta_function::new_index] = [](sol::this_state L) noexcept
    {
        lua_print_error(L, "Attempted to write to read-only table `tr`!");
    };
    tr_metatable[sol::metatable_key] = tr;

    // Create the `Player` userdata.
    auto player_ud = m_lua.new_usertype<Player>(
        "Player",
        sol::meta_function::construct,
        [this](sol::this_state L, sol::stack_object index) noexcept
        { return index.is<i32>() ? sol::make_object(L, get_player(index.as<i32>())) : sol::nil; },
        sol::call_constructor,
        [this](sol::this_state L, sol::stack_object index) noexcept
        { return index.is<i32>() ? sol::make_object(L, get_player(index.as<i32>())) : sol::nil; },
        "valid",
        [](sol::stack_object self) noexcept { return self.is<Player *>() ? self.as<Player *>()->valid() : false; },
        "get_index",
        [](sol::stack_object self) noexcept { return self.is<Player *>() ? self.as<Player *>()->get_index() : 0; },
        "get_name",
        [](sol::stack_object self) noexcept { return self.is<Player *>() ? self.as<Player *>()->get_name() : ""; },
        "get_ip",
        [](sol::stack_object self, sol::stack_object remove_port) noexcept -> std::string
        {
            if (!self.is<Player *>())
            {
                return "";
            }

            bool value = true;
            if (remove_port.is<bool>())
            {
                value = remove_port.as<bool>();
            }

            return self.as<Player *>()->get_ip(value);
        },
        "get_user_id",
        [](sol::stack_object self) noexcept { return self.is<Player *>() ? self.as<Player *>()->get_user_id() : -1; });

    auto player_table                               = m_lua.create_named_table("Player");
    auto player_metatable                           = m_lua.create_table_with();
    player_metatable[sol::meta_function::index]     = player_ud;
    player_metatable[sol::meta_function::new_index] = [](sol::this_state L)
    {
        lua_print_error(L, "Attempted to write to read-only usertype `Player`!");
    };
    player_table[sol::metatable_key] = player_metatable;

    // Create the `Game` userdata.
    auto game_ud = m_lua.new_usertype<Game>(
        "Game",
        sol::meta_function::construct,
        sol::no_constructor,
        "get_mod_name",
        [](sol::this_state L, sol::stack_object self) noexcept
        { return self.is<Game *>() ? sol::make_object(L, self.as<Game *>()->mod_name) : sol::nil; });

    tr["game"] = &g_game;

    auto game_table                               = m_lua.create_named_table("Game");
    auto game_metatable                           = m_lua.create_table_with();
    game_metatable[sol::meta_function::index]     = game_ud;
    game_metatable[sol::meta_function::new_index] = [](sol::this_state L)
    {
        lua_print_error(L, "Attempted to write to read-only usertype `Game`!");
    };
    game_table[sol::metatable_key] = game_metatable;
}

LuaScriptState::~LuaScriptState() noexcept
{
    std::scoped_lock lock{m_exec_mutex};
}

[[nodiscard]] std::optional<LuaScriptState::CallbackID> LuaScriptState::str_to_callback_id(const std::string &name) const noexcept
{
    auto found = m_callback_names.find(name);
    if (found == m_callback_names.end())
    {
        return {};
    }

    return found->second;
}

[[nodiscard]] Player *LuaScriptState::get_player(i32 index) const noexcept
{
    return index >= 1 && (usize)(index - 1) < m_players.size() ? (Player *)&m_players[index - 1] : nullptr;
}

[[nodiscard]] Player *LuaScriptState::get_player(edict_t *edict) const noexcept
{
    return edict != nullptr ? get_player(g_game.engine->IndexOfEdict(edict)) : nullptr;
}

tl::expected<void, std::string> LuaScriptState::run_script_file(const std::filesystem::path &full_path) noexcept
{
    std::scoped_lock lock{m_exec_mutex};

    // Only allow requiring from the directory the script was run from.
    auto old_path  = m_lua["package"]["path"];
    auto old_cpath = m_lua["package"]["cpath"];

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

    auto result = m_lua.safe_script_file(full_path.string(), sol::script_pass_on_error);

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
    std::scoped_lock lock{m_exec_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_load])
    {
        if (auto result = cb(); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] `on_load` error: {}", err.what());
        }
    }
}

void LuaScriptState::on_script_reset() noexcept
{
    std::scoped_lock lock{m_exec_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_script_reset])
    {
        if (auto result = cb(); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] `on_script_reset` error: {}", err.what());
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
            utl::print_error("[LuaScriptState] `on_level_init` error: {}", err.what());
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
            utl::print_error("[LuaScriptState] `on_level_shutdown` error: {}", err.what());
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
            utl::print_error("[LuaScriptState] `on_game_frame` error: {}", err.what());
        }
    }

    // TODO: Handle garbage collection if needed.
}

PLUGIN_RESULT
LuaScriptState::on_client_connect(
    bool *allow_connect, edict_t *edict, std::string_view name, std::string_view address, char *reject, i32 max_reject_len) noexcept
{
    utl::print_info("address = {}", address);

    // Add player to cache.
    i32 player_idx = g_game.engine->IndexOfEdict(edict);
    if (player_idx <= 0)
    {
        return PLUGIN_STOP;
    }

    if (m_players.size() < (usize)player_idx)
    {
        m_players.resize(player_idx);
    }

    auto *player = &m_players[player_idx - 1];
    *player      = Player{edict, name, address};

    {
        std::scoped_lock lock{m_exec_mutex};

        for (auto &&cb : m_callbacks[CallbackID::on_client_connect])
        {
            if (auto result = cb(player); !result.valid())
            {
                sol::error err = result;
                utl::print_error("[LuaScriptState] `on_game_frame` error: {}", err.what());
            }
            else
            {
                bool allow         = false;
                auto deny_result   = result[0];
                auto reason_result = result[1];

                if (deny_result.is<bool>())
                {
                    allow = deny_result.get<bool>();
                }

                if (allow_connect != nullptr)
                {
                    *allow_connect = allow;
                }

                if (!allow)
                {
                    std::string reason{};
                    if (reason_result.is<std::string>())
                    {
                        reason = reason_result.get<std::string>();
                    }

                    if (!reason.empty())
                    {
                        if (reason.size() < (usize)(max_reject_len - 1))
                        {
                            std::memcpy(reject, reason.data(), reason.size());
                            reject[reason.size()] = '\0';
                        }
                        else
                        {
                            reason = reason.substr(0, max_reject_len - 4) + "...";
                            std::memcpy(reject, reason.data(), reason.size());
                            reject[reason.size()] = '\0';
                        }
                    }

                    return PLUGIN_STOP;
                }
            }
        }
    }

    return PLUGIN_CONTINUE;
}

void LuaScriptState::on_client_disconnect(edict_t *edict) noexcept
{
    auto *player = get_player(edict);
    if (player == nullptr)
    {
        return;
    }

    {
        std::scoped_lock lock{m_exec_mutex};

        for (auto &&cb : m_callbacks[CallbackID::on_client_disconnect])
        {
            if (auto result = cb(player); !result.valid())
            {
                sol::error err = result;
                utl::print_error("[LuaScriptState] `on_client_disconnect` error: {}", err.what());
            }
        }
    }

    // Reset player in cache.
    *player = {};
}

void LuaScriptState::on_client_spawn(edict_t *edict, std::string_view name) noexcept
{
    std::scoped_lock lock{m_exec_mutex};

    for (auto &&cb : m_callbacks[CallbackID::on_client_spawn])
    {
        if (auto result = cb(edict, name); !result.valid())
        {
            sol::error err = result;
            utl::print_error("[LuaScriptState] `on_client_spawn` error: {}", err.what());
        }
    }
}
