#pragma once

#include "common.hpp"
#include "type.hpp"
#include "utl.hpp"
#include <string>

// Engine stuff.
using CreateInterfaceFn      = void *(TR_CCALL *)(cstr name, i32 *return_code);
using InstantiateInterfaceFn = void *(TR_CCALL *)();

class KeyValues;
class CCommand;

using QueryCvarCookie_t = i32;

constexpr f32 MINIMUM_TICK_INTERVAL = 0.001f;
constexpr f32 MAXIMUM_TICK_INTERVAL = 0.1f;

enum : i32
{
    IFACE_OK = 0,
    IFACE_FAILED,
};

enum PLUGIN_RESULT : i32
{
    PLUGIN_CONTINUE = 0,
    PLUGIN_OVERRIDE,
    PLUGIN_STOP,
};

enum EQueryCvarValueStatus : i32
{
    eQueryCvarValueStatus_ValueIntact = 0,
    eQueryCvarValueStatus_CvarNotFound,
    eQueryCvarValueStatus_NotACvar,
    eQueryCvarValueStatus_CvarProtected,
};

class edict_t
{
public:
};

class Player
{
public:
    Player() noexcept = default;
    explicit Player(edict_t *edict) noexcept;

    [[nodiscard]] i32 get_user_id() const noexcept
    {
        return m_user_id;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return m_valid;
    }

private:
    edict_t *m_edict{};
    i32      m_user_id{-1};
    bool     m_valid{};
};

class InterfaceReg
{
public:
    InstantiateInterfaceFn m_CreateFn;
    cstr                   m_pName;
    InterfaceReg          *m_pNext;
};

class CGlobalVars
{
public:
};

class CServerGameDLL
{
public:
};

class CVEngineServer
{
public:
    [[nodiscard]] i32 GetPlayerUserId(edict_t *edict) const noexcept
    {
        return utl::get_virtual<i32(TR_THISCALL *)(decltype(this), edict_t *)>(this, 15)(this, edict);
    }

    [[nodiscard]] i32 IndexOfEdict(edict_t *edict) const noexcept
    {
        return utl::get_virtual<i32(TR_THISCALL *)(decltype(this), edict_t *)>(this, 18)(this, edict);
    }
};

class CPlayerInfoManager
{
public:
    // virtual IPlayerInfo *GetPlayerInfo( edict_t *pEdict );

    [[nodiscard]] CGlobalVars *GetGlobalVars() const noexcept
    {
        return utl::get_virtual<CGlobalVars *(TR_THISCALL *)(decltype(this))>(this, 1)(this);
    }
};

// Implemented as `ISERVERPLUGINCALLBACKS003`.
class IServerPluginCallbacks
{
public:
    virtual bool          Load(CreateInterfaceFn interface_factory, CreateInterfaceFn gameserver_factory)                               = 0;
    virtual void          Unload()                                                                                                      = 0;
    virtual void          Pause()                                                                                                       = 0;
    virtual void          UnPause()                                                                                                     = 0;
    virtual cstr          GetPluginDescription()                                                                                        = 0;
    virtual void          LevelInit(cstr map_name)                                                                                      = 0;
    virtual void          ServerActivate(edict_t *edict_list, i32 edict_count, i32 client_max)                                          = 0;
    virtual void          GameFrame(bool simulating)                                                                                    = 0;
    virtual void          LevelShutdown()                                                                                               = 0;
    virtual void          ClientActive(edict_t *edict)                                                                                  = 0;
    virtual void          ClientDisconnect(edict_t *edict)                                                                              = 0;
    virtual void          ClientPutInServer(edict_t *edict, cstr player_name)                                                           = 0;
    virtual void          SetCommandClient(i32 index)                                                                                   = 0;
    virtual void          ClientSettingsChanged(edict_t *edict)                                                                         = 0;
    virtual PLUGIN_RESULT ClientConnect(bool *allow_connect, edict_t *edict, cstr name, cstr address, char *reject, i32 max_reject_len) = 0;
    virtual PLUGIN_RESULT ClientCommand(edict_t *edict, const CCommand &args)                                                           = 0;
    virtual PLUGIN_RESULT NetworkIDValidated(cstr username, cstr network_id)                                                            = 0;
    virtual void
    OnQueryCvarValueFinished(QueryCvarCookie_t cookie, edict_t *edict, EQueryCvarValueStatus status, cstr cvar_name, cstr cvar_value) = 0;
    virtual void OnEdictAllocated(edict_t *edict)                                                                                     = 0;
    virtual void OnEdictFreed(edict_t *edict)                                                                                         = 0;
};

class IGameEventListener
{
public:
    virtual ~IGameEventListener() noexcept       = default;
    virtual void FireGameEvent(KeyValues *event) = 0;
};

// Game data.
class Game
{
public:
    Game() noexcept = default;

    std::string     mod_name{};
    CGlobalVars    *globals{};
    CVEngineServer *engine{};
};

inline Game g_game{};
