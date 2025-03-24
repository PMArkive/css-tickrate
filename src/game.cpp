#include "game.hpp"

Player::Player(edict_t *edict) noexcept : m_edict{edict}
{
    if (edict == nullptr)
    {
        return;
    }

    m_user_id = g_game.engine->GetPlayerUserId(m_edict);
    if (m_user_id == -1)
    {
        return;
    }

    m_valid = true;
}
