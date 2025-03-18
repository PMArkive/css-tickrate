#include "common.hpp"
#include "type.hpp"
#include "utl.hpp"
#include "os.hpp"
#include "lua/lua_loader.hpp"
#include <safetyhook/safetyhook.hpp>
#include <string_view>
#include <charconv>
#include <filesystem>

// Engine stuff.
using CreateInterfaceFn      = void *(TR_CCALL *)(cstr name, i32 *return_code);
using InstantiateInterfaceFn = void *(TR_CCALL *)();

struct edict_t;
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

class InterfaceReg
{
public:
    InstantiateInterfaceFn m_CreateFn;
    cstr                   m_pName;
    InterfaceReg          *m_pNext;
};

class CServerGameDLL
{
public:
};

// ISERVERPLUGINCALLBACKS003
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

// Global variables, etc.
LuaScriptLoader  g_lua_loader{};
u16              g_desired_tickrate{};
SafetyHookInline g_GetTickInterval_hook{};

class Hooked_CServerGameDLL : public CServerGameDLL
{
public:
    static f32 TR_THISCALL hooked_GetTickInterval([[maybe_unused]] CServerGameDLL *instance) noexcept
    {
        f32 interval = 1.0f / (f32)g_desired_tickrate;

        return interval;
    }
};

// Dummy function so we can find our own module.
void find_me() noexcept {}

class TickratePlugin final : public IServerPluginCallbacks,
                             public IGameEventListener
{
public:
    TickratePlugin() noexcept           = default;
    ~TickratePlugin() noexcept override = default;

    bool Load(CreateInterfaceFn interface_factory, CreateInterfaceFn gameserver_factory) noexcept override
    {
        utl::print_info("Loading...\n");

        // Find our own module.
        u8 *our_module = os_get_module((u8 *)find_me);
        if (our_module == nullptr)
        {
            utl::print_error("Failed to get our own module.\n");
            return false;
        }

        auto our_module_full_path = os_get_module_full_path(our_module);
        if (our_module_full_path.empty())
        {
            utl::print_error("Failed to get our own module's full path.\n");
            return false;
        }

        // Make sure autorun directory exists.
        std::filesystem::path autorun_dir{};
        {
            std::filesystem::path addons_path = our_module_full_path;
            addons_path                       = addons_path.parent_path().make_preferred();

            auto plugin_dir = addons_path / "tickrate";
            autorun_dir     = plugin_dir / "autorun";

            std::error_code ec;
            if (std::filesystem::create_directories(autorun_dir, ec); ec != std::error_code{})
            {
                utl::print_error("Failed to create autorun directory.\n");
                return false;
            }
        }

        u8 *server_createinterface = (u8 *)gameserver_factory;
        u8 *server_module          = os_get_module(server_createinterface);
        if (server_module == nullptr)
        {
            utl::print_error("Failed to get server module.\n");
            return false;
        }

        auto cmdline = os_get_split_command_line();
        if (cmdline.empty())
        {
            utl::print_error("Failed to get command line.\n");
            return false;
        }

        std::string_view mod_value{}, tickrate_value{};
        for (usize i{}; i < cmdline.size(); ++i)
        {
            // Check if the next entry has a value. Some options require this.
            if (i + 1 < cmdline.size())
            {
                if (mod_value.empty() && cmdline[i] == "-game")
                {
                    mod_value = cmdline[i + 1];
                }

                if (tickrate_value.empty() && cmdline[i] == "-tickrate")
                {
                    tickrate_value = cmdline[i + 1];
                }
            }
        }

        if (mod_value.empty())
        {
            mod_value = "hl2";
        }

        utl::print_info("mod = {}\n", mod_value);

        if (tickrate_value.empty())
        {
            utl::print_error("Bad tickrate: Failed to find `-tickrate` command line string.\n");
            return false;
        }

        auto [ptr, ec] = std::from_chars(tickrate_value.data(), tickrate_value.data() + tickrate_value.size(), g_desired_tickrate);
        if (ec != std::errc{})
        {
            utl::print_error("Bad tickrate: Failed to convert `-tickrate` command line value.\n");
            return false;
        }

        // This is not a bug. They're swapped for a reason (we convert them to an int instead of comparing the float).
        constexpr u16 min_tickrate = (u16)(1.0f / MAXIMUM_TICK_INTERVAL) + 1;
        constexpr u16 max_tickrate = (u16)(1.0f / MINIMUM_TICK_INTERVAL) + 1;

        if (g_desired_tickrate < min_tickrate)
        {
            utl::print_error(
                "Bad tickrate: `-tickrate` command line value is too low (Desired tickrate is {}, minimum is {}). Server will continue with "
                "default tickrate.\n",
                g_desired_tickrate,
                min_tickrate);
            return false;
        }
        else if (g_desired_tickrate > max_tickrate)
        {
            utl::print_error(
                "Bad tickrate: `-tickrate` command line value is too high (Desired tickrate is {}, maximum is {}). Server will continue with "
                "default tickrate.\n",
                g_desired_tickrate,
                max_tickrate);
            return false;
        }

        utl::print_info("Desired tickrate is {}.\n", g_desired_tickrate);

        InterfaceReg *regs;

        // Check for the `s_pInterfaceRegs` symbol first.
        if (u8 *regs_symbol = os_get_procedure(server_module, "s_pInterfaceRegs"); regs_symbol != nullptr)
        {
            regs = *(InterfaceReg **)regs_symbol;
        }
        else
        {
            // No symbol was found so we have to disasm manually.
            // First we check for a jump thunk. Some versions of the game have this for some reason. If there isn't one then we don't worry about it.
            for (;;)
            {
                auto thunk_disasm_result = utl::disasm(server_createinterface);
                if (!thunk_disasm_result)
                {
                    utl::print_error("Failed to decode first instruction in `CreateInterface`: {}\n", thunk_disasm_result.error().status_str());
                    return false;
                }

                auto &&thunk_disasm = *thunk_disasm_result;
                if (thunk_disasm.ix.mnemonic != ZYDIS_MNEMONIC_JMP)
                {
                    break;
                }

                server_createinterface += thunk_disasm.ix.length + (i32)thunk_disasm.operands[0].imm.value.s;
            }

            // Find the first `mov reg, mem`.
            auto regs_disasm_result = utl::disasm_for_each(
                server_createinterface,
                ZYDIS_MAX_INSTRUCTION_LENGTH * 25, // I hope this is enough :P
                [](auto &&result) noexcept
                {
                    // x86-64 is RIP-relative. x86-32 is absolute.
                    constexpr ZyanU16 op_size  = TR_ARCH_X86_64 == 1 ? 64 : 32;
                    constexpr auto    mem_base = TR_ARCH_X86_64 == 1 ? ZYDIS_REGISTER_RIP : ZYDIS_REGISTER_NONE;

                    return result.ix.mnemonic == ZYDIS_MNEMONIC_MOV && result.ix.operand_count_visible == 2
                        && result.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER && result.operands[0].size == op_size
                        && result.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY && result.operands[1].size == op_size
                        && result.operands[1].mem.segment == ZYDIS_REGISTER_DS && result.operands[1].mem.base == mem_base;
                });
            if (!regs_disasm_result)
            {
                utl::print_error("Failed to find instruction containing `s_pInterfaceRegs`: {}\n", regs_disasm_result.error().status_str());
                return false;
            }

            auto &&regs_disasm = *regs_disasm_result;

            // x86-64 is RIP-relative. x86-32 is absolute.
#if TR_ARCH_X86_64
            regs = *(InterfaceReg **)(regs_disasm.ip + regs_disasm.ix.length + (i32)regs_disasm.operands[1].mem.disp.value);
#else
            regs = *(InterfaceReg **)((usize)regs_disasm.operands[1].mem.disp.value);
#endif
        }

        if (regs == nullptr)
        {
            utl::print_error("Failed to find `s_pInterfaceRegs` (null).\n");
            return false;
        }

        // TODO: If this becomes an issue, we can just search for latest version... but for now, only the latest should be exist.
        CServerGameDLL *servergame{};

        for (auto *it = regs; it != nullptr; it = it->m_pNext)
        {
            if (it->m_pName == nullptr)
            {
                continue;
            }

            if (utl::sv_contains(it->m_pName, "ServerGameDLL"))
            {
                servergame = (CServerGameDLL *)it->m_CreateFn();
                break;
            }
        }

        if (servergame == nullptr)
        {
            utl::print_error("Failed to find `ServerGameDLL` interface.\n");
            return false;
        }

        utl::print_info("Applying hooks...\n");

        // TODO: There's a chance that this virtual function might not always be index 10. Will need testing.
        u8 *fn = utl::get_virtual(servergame, 10);

        // TODO: Switch to global VMT hooks when it's available.
        auto hook_result = safetyhook::InlineHook::create(fn, Hooked_CServerGameDLL::hooked_GetTickInterval);
        if (!hook_result)
        {
            auto &&err = hook_result.error();
            utl::print_error(
                "Failed to hook `CServerGameDLL::GetTickInterval` function: {} @ 0x{:X}\n", utl::safetyhookinline_error_str(err), (usize)err.ip);

            return false;
        }

        g_GetTickInterval_hook = std::move(*hook_result);

        // Set up Lua.
        if (!g_lua_loader.init(autorun_dir))
        {
            utl::print_error("Failed to initialize Lua loader.\n");
            return false;
        }

        g_lua_loader.on_load();

        utl::print_info("Loaded!\n");

        return true;
    }

    void Unload() noexcept override
    {
        g_GetTickInterval_hook = {};

        utl::print_info("Unloaded.\n");
    }

    void Pause() noexcept override {}

    void UnPause() noexcept override {}

    cstr GetPluginDescription() noexcept override
    {
        return "Tickrate (angelfor3v3r)";
    }

    void LevelInit(cstr map_name) noexcept override {}

    void ServerActivate(edict_t *edict_list, i32 edict_count, i32 client_max) noexcept override {}

    void GameFrame(bool simulating) noexcept override
    {
        g_lua_loader.on_game_frame(simulating);
    }

    void LevelShutdown() noexcept override {}

    void ClientActive(edict_t *edict) noexcept override {}

    void ClientDisconnect(edict_t *edict) noexcept override {}

    void ClientPutInServer(edict_t *edict, cstr player_name) noexcept override {}

    void SetCommandClient(i32 index) noexcept override {}

    void ClientSettingsChanged(edict_t *edict) noexcept override {}

    PLUGIN_RESULT
    ClientConnect(bool *allow_connect, edict_t *edict, cstr name, cstr address, char *reject, i32 max_reject_len) noexcept override
    {
        return PLUGIN_CONTINUE;
    }

    PLUGIN_RESULT ClientCommand(edict_t *edict, const CCommand &args) noexcept override
    {
        return PLUGIN_CONTINUE;
    }

    PLUGIN_RESULT NetworkIDValidated(cstr username, cstr network_id) noexcept override
    {
        return PLUGIN_CONTINUE;
    }

    void OnQueryCvarValueFinished(
        QueryCvarCookie_t cookie, edict_t *edict, EQueryCvarValueStatus status, cstr cvar_name, cstr cvar_value) noexcept override
    {}

    void OnEdictAllocated(edict_t *edict) noexcept override {}

    void OnEdictFreed(edict_t *edict) noexcept override {}

    void FireGameEvent(KeyValues *event) noexcept override {}
};

TickratePlugin g_tickrate_plugin{};

extern "C" TR_DLLEXPORT void *CreateInterface(cstr name, i32 *return_code) noexcept
{
    TickratePlugin *result{};

    // First call should be the latest version.
    // v2 added `OnQueryCvarValueFinished`.
    // v3 added `OnEdictAllocated`/`OnEdictFreed`.
    if (utl::sv_contains(name, "ISERVERPLUGINCALLBACKS"))
    {
        result = &g_tickrate_plugin;
    }

    if (return_code != nullptr)
    {
        *return_code = result != nullptr ? IFACE_OK : IFACE_FAILED;
    }

    return result;
}
