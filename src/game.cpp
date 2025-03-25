#include "game.hpp"

Player::Player(edict_t *edict) noexcept : m_edict{edict}
{
    m_ent_index = g_game.engine->IndexOfEdict(m_edict);
    if (m_ent_index <= 0)
    {
        return;
    }

    // auto *steam_id = g_game.engine->GetClientSteamIDByPlayerIndex(m_ent_index);
    // if (steam_id == nullptr)
    // {
    //     return;
    // }

    // m_steam_id = *steam_id;
    m_user_id  = g_game.engine->GetPlayerUserId(m_edict);
    if (m_user_id == -1)
    {
        return;
    }

    m_valid = true;
}
