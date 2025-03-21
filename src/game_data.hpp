#pragma once

#include <string>

class GameData
{
public:
    GameData() noexcept = default;

    std::string mod_name{};
};

inline GameData g_game_data{};
