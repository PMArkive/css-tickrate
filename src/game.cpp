#include "game.hpp"

Player::Player(edict_t *edict, std::string_view name, std::string_view address) noexcept : m_edict{edict}, m_name{name}, m_address{address}
{
    m_index = g_game.engine->IndexOfEdict(m_edict);
    if (m_index <= 0)
    {
        return;
    }

    // auto *steam_id = g_game.engine->GetClientSteamIDByPlayerIndex(m_ent_index);
    // if (steam_id == nullptr)
    // {
    //     return;
    // }

    // m_steam_id = *steam_id;
    m_user_id = g_game.engine->GetPlayerUserId(m_edict);
    if (m_user_id == -1)
    {
        return;
    }

    m_valid = true;
}

std::string Player::get_ip(bool remove_port) noexcept
{
    if (remove_port)
    {
        // Cache it without the port number.
        if (m_address_no_port.empty())
        {
            if (auto delim = m_address.find(':'); delim != std::string::npos)
            {
                m_address_no_port = m_address.substr(0, delim);
            }
        }

        return m_address_no_port;
    }

    return m_address;
}
