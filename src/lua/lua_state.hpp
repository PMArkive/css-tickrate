#pragma once

#include "type.hpp"
#include <tl/expected.hpp>
#include <sol/sol.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <filesystem>

class LuaScriptState
{
public:
    LuaScriptState() noexcept = default;
    LuaScriptState(bool is_main_state) noexcept;

    // TODO: State creation/deletion.

    // Run a script file.
    tl::expected<void, std::string> run_script_file(const std::filesystem::path &full_path) noexcept;

    // Callbacks.
    void on_load() noexcept;
    void on_script_reset() noexcept;
    void on_game_frame(bool simulating) noexcept;

    // State reference.
    [[nodiscard]] auto &lua() noexcept
    {
        return m_lua;
    }

private:
    bool                 m_is_main_state{};
    sol::state           m_lua{};
    std::recursive_mutex m_lua_mutex{};

    // Callbacks.
    enum class CallbackID : u8
    {
        on_load = 0,
        on_script_reset,
        on_game_frame,
    };

    const std::unordered_map<std::string, CallbackID> m_callback_names{
        {"on_load", CallbackID::on_load},
        {"on_script_reset", CallbackID::on_script_reset},
        {"on_game_frame", CallbackID::on_game_frame},
    };

    [[nodiscard]] std::optional<CallbackID> str_to_callback_id(const std::string &name) const noexcept;

    std::unordered_map<CallbackID, std::vector<sol::protected_function>> m_callbacks{};
};
