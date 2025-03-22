#pragma once

#include "type.hpp"
#include "lua_state.hpp"
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <string_view>
#include <filesystem>

class LuaScriptLoader
{
public:
    LuaScriptLoader() noexcept = default;

    // Initialize the class.
    bool init(const std::filesystem::path &autorun_dir) noexcept;

    // Lock all the Lua states.
    void lock() noexcept;

    // Unlock all the Lua states.
    void unlock() noexcept;

    // Create a new Lua state.
    lua_State *create_state() noexcept;

    // Delete a lua state.
    void delete_state(lua_State *state) noexcept;

    // Reset everything and run autorun scripts again.
    void reset_scripts() noexcept;

    // Return a state by index. Passing 0 or no arg will return the main state.
    // The main state is always the first entry in the states vector.
    [[nodiscard]] auto &state(usize index = 0) const noexcept
    {
        return index == 0 ? m_main_state : m_states[index];
    }

    // Callbacks.
    void on_load() noexcept;
    void on_level_init(std::string_view map_name) noexcept;
    void on_level_shutdown() noexcept;
    void on_game_frame(bool simulating) noexcept;

private:
    std::filesystem::path                        m_autorun_dir{};
    std::shared_ptr<LuaScriptState>              m_main_state{};
    std::vector<std::shared_ptr<LuaScriptState>> m_states{};
    std::recursive_mutex                         m_lua_mutex{};
    std::atomic<usize>                           m_lock_depth{};
    std::vector<lua_State *>                     m_states_to_delete{};
};
