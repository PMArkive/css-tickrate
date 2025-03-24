#pragma once

#include "type.hpp"
#include "game.hpp"
#include <tl/expected.hpp>
#include <sol/sol.hpp>
#include <mutex>
#include <optional>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

class LuaScriptState
{
public:
    LuaScriptState() noexcept = default;
    explicit LuaScriptState(bool is_main_state) noexcept;

    ~LuaScriptState() noexcept;

    // Run a script file.
    tl::expected<void, std::string> run_script_file(const std::filesystem::path &full_path) noexcept;

    // Lock the Lua execution mutex.
    void lock() noexcept
    {
        m_exec_mutex.lock();
    }

    // Unlock the Lua execution mutex.
    void unlock() noexcept
    {
        m_exec_mutex.unlock();
    }

    // State reference.
    [[nodiscard]] auto &lua() noexcept
    {
        return m_lua;
    }

    // Callbacks.
    void          on_script_reset() noexcept;
    void          on_load() noexcept;
    void          on_level_init(std::string_view map_name) noexcept;
    void          on_level_shutdown() noexcept;
    void          on_game_frame(bool simulating) noexcept;
    PLUGIN_RESULT on_client_connect(
        bool *allow_connect, edict_t *edict, std::string_view name, std::string_view address, char *reject, i32 max_reject_len) noexcept;

private:
    bool                 m_is_main_state{};
    sol::state           m_lua{};
    std::recursive_mutex m_exec_mutex{};

    // Callbacks.
    enum class CallbackID : u8
    {
        on_load = 0,
        // on_unload,
        on_script_reset,
        on_game_frame,
        on_level_init,
        on_level_shutdown,
        on_client_connect,
    };

    const std::unordered_map<std::string, CallbackID> m_callback_names{
        {"on_load", CallbackID::on_load},
        {"on_script_reset", CallbackID::on_script_reset},
        {"on_game_frame", CallbackID::on_game_frame},
        {"on_level_init", CallbackID::on_level_init},
        {"on_level_shutdown", CallbackID::on_level_shutdown},
        {"on_client_connect", CallbackID::on_client_connect},
    };

    [[nodiscard]] std::optional<CallbackID> str_to_callback_id(const std::string &name) const noexcept;

    std::unordered_map<CallbackID, std::vector<sol::protected_function>> m_callbacks{};
};
