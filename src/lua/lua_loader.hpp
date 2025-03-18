#pragma once

#include "type.hpp"
#include "lua_state.hpp"
#include <memory>
#include <mutex>
#include <vector>
#include <filesystem>

class LuaScriptLoader
{
public:
    LuaScriptLoader() noexcept = default;

    bool init(const std::filesystem::path &autorun_dir) noexcept;

    // Reset everything and run autorun scripts again.
    void reset_scripts() noexcept;

    // Callbacks.
    void on_load() noexcept;
    void on_game_frame(bool simulating) noexcept;

    // Return a state by index. Passing 0 or no arg will return the main state.
    // The main state is always the first entry in the states vector.
    [[nodiscard]] auto &state(usize index = 0) const noexcept
    {
        return index == 0 ? m_main_state : m_states[index];
    }

private:
    std::filesystem::path                        m_autorun_dir{};
    std::shared_ptr<LuaScriptState>              m_main_state{};
    std::vector<std::shared_ptr<LuaScriptState>> m_states{};
    std::recursive_mutex                         m_mutex{};
    std::vector<lua_State *>                     m_states_to_delete{};
};
