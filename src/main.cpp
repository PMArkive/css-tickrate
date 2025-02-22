#include "common.hpp"
#include "type.hpp"
#include "os.hpp"
#include <fmt/format.h>
#include <Zydis/Zydis.h>
#include <safetyhook.hpp>
#include <string_view>
#include <expected>

using CreateInterfaceFn      = void *(TR_CCALL *)(const char *name, i32 *return_code);
using InstantiateInterfaceFn = void *(TR_CCALL *)();

struct edict_t;
class KeyValues;
class CCommand;

using QueryCvarCookie_t = i32;

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

struct DisasmResult
{
    u8                     *ip{};
    ZydisDecodedInstruction ix{};
    ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT]{};
};

struct DisasmError
{
    u8        *ip{};
    ZyanStatus error{ZYAN_STATUS_SUCCESS};
};

[[nodiscard]] std::expected<DisasmResult, DisasmError> disasm(u8 *ip, usize len = ZYDIS_MAX_INSTRUCTION_LENGTH) noexcept
{
    static ZydisDecoder decoder;
    if (static bool once{}; !once)
    {
#if TR_ARCH_X86_64
        auto status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
#else
        auto status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
#endif
        if (!ZYAN_SUCCESS(status))
        {
            return std::unexpected{DisasmError{ip, status}};
        }

        once = true;
    }

    DisasmResult result{};
    auto         status = ZydisDecoderDecodeFull(&decoder, ip, len, &result.ix, result.operands);
    if (!ZYAN_SUCCESS(status))
    {
        return std::unexpected{DisasmError{ip, status}};
    }

    result.ip = ip;

    return result;
}

template <class Pred>
std::expected<DisasmResult, DisasmError> disasm_for_each(u8 *ip, usize len, Pred &&pred) noexcept
{
    u8 *end = ip + len;

    for (u8 *i = ip; i < end;)
    {
        auto result = disasm(i, (usize)(end - i));
        if (!result.has_value())
        {
            return std::unexpected{result.error()};
        }

        auto &&value = result.value();
        if (pred(value))
        {
            return value;
        }

        i += value.ix.length;
    }

    return {};
}

template <class T>
T get_virtual(const void *object, u16 index) noexcept
{
    return (*(T **)object)[index];
}

class InterfaceReg
{
public:
    InstantiateInterfaceFn m_CreateFn;
    const char            *m_pName;
    InterfaceReg          *m_pNext;
};

class CCommandLine
{
public:
    [[nodiscard]] i32 ParmValue(const char *parm, i32 default_value) const noexcept
    {
        return get_virtual<i32(TR_THISCALL *)(decltype(this), const char *, i32)>(this, 7)(this, parm, default_value);
    }

    [[nodiscard]] i32 FindParm(const char *parm) const noexcept
    {
        return get_virtual<i32(TR_THISCALL *)(decltype(this), const char *)>(this, 10)(this, parm);
    }
};

class CServerGameDLL
{
public:
};

class IServerPluginCallbacks
{
public:
    virtual bool        Load(CreateInterfaceFn interface_factory, CreateInterfaceFn gameserver_factory) = 0;
    virtual void        Unload()                                                                        = 0;
    virtual void        Pause()                                                                         = 0;
    virtual void        UnPause()                                                                       = 0;
    virtual const char *GetPluginDescription()                                                          = 0;
    virtual void        LevelInit(const char *map_name)                                                 = 0;
    virtual void        ServerActivate(edict_t *edict_list, i32 edict_count, i32 client_max)            = 0;
    virtual void        GameFrame(bool simulating)                                                      = 0;
    virtual void        LevelShutdown()                                                                 = 0;
    virtual void        ClientActive(edict_t *edict)                                                    = 0;
    virtual void        ClientDisconnect(edict_t *edict)                                                = 0;
    virtual void        ClientPutInServer(edict_t *edict, const char *player_name)                      = 0;
    virtual void        SetCommandClient(i32 index)                                                     = 0;
    virtual void        ClientSettingsChanged(edict_t *edict)                                           = 0;
    virtual PLUGIN_RESULT
    ClientConnect(bool *allow_connect, edict_t *edict, const char *name, const char *address, char *reject, i32 max_reject_len) = 0;
    virtual PLUGIN_RESULT ClientCommand(edict_t *edict, const CCommand &args)                                                   = 0;
    virtual PLUGIN_RESULT NetworkIDValidated(const char *user_name, const char *network_id)                                     = 0;
    virtual void          OnQueryCvarValueFinished(
                 QueryCvarCookie_t cookie, edict_t *edict, EQueryCvarValueStatus status, const char *cvar_name, const char *cvar_value) = 0;
    virtual void OnEdictAllocated(edict_t *edict)                                                                                       = 0;
    virtual void OnEdictFreed(edict_t *edict)                                                                                           = 0;
};

class IGameEventListener
{
public:
    virtual ~IGameEventListener() noexcept       = default;
    virtual void FireGameEvent(KeyValues *event) = 0;
};

using Warning_fn = void(TR_CCALL *)(const char *, ...);
using Msg_fn     = void(TR_CCALL *)(const char *, ...);

Warning_fn    g_Warning{};
Msg_fn        g_Msg{};
CCommandLine *g_cmdline{};
SafetyHookVmt g_servergame_hook{};
SafetyHookVm  g_GetTickInterval_hook{};
i32           g_desired_tickrate{};

template <class... Args>
void warn(std::string_view fmt_str, Args &&...args) noexcept
{
    if (g_Warning == nullptr)
    {
        return;
    }

    g_Warning("[CS:S Tickrate] [warning] %s", fmt::vformat(fmt_str, fmt::make_format_args(args...)).c_str());
}

template <class... Args>
void info(std::string_view fmt_str, Args &&...args) noexcept
{
    if (g_Msg == nullptr)
    {
        return;
    }

    g_Msg("[CS:S Tickrate] [info] %s", fmt::vformat(fmt_str, fmt::make_format_args(args...)).c_str());
}

class Hooked_CServerGameDLL : public CServerGameDLL
{
public:
    f32 hooked_GetTickInterval() const noexcept
    {
        f32 interval = 1.0f / g_desired_tickrate;

        return interval;
    }
};

class TickratePlugin final : public IServerPluginCallbacks,
                             public IGameEventListener
{
public:
    TickratePlugin() noexcept           = default;
    ~TickratePlugin() noexcept override = default;

    bool Load(CreateInterfaceFn interface_factory, CreateInterfaceFn gameserver_factory) noexcept override
    {
        info("Loading...");

        constexpr std::string_view tier0_lib_name = TR_OS_WINDOWS == 1 ? "tier0.dll" : "libtier0_srv.so";

        auto tier0_lib = os_get_module(tier0_lib_name);
        if (tier0_lib == nullptr)
        {
            return false;
        }

        g_Warning = os_get_procedure<Warning_fn>(tier0_lib, "Warning");
        if (g_Warning == nullptr)
        {
            return false;
        }

        g_Msg = os_get_procedure<Msg_fn>(tier0_lib, "Msg");
        if (g_Msg == nullptr)
        {
            warn("Failed to find \"Msg\" procedure.\n");
            return false;
        }

        auto *CommandLine_Tier0 = os_get_procedure<CCommandLine *(TR_CCALL *)()>(tier0_lib, "CommandLine_Tier0");
        if (CommandLine_Tier0 == nullptr)
        {
            warn("Failed to find \"CommandLine_Tier0\" procedure.\n");
            return false;
        }

        g_cmdline = CommandLine_Tier0();
        if (g_cmdline == nullptr)
        {
            warn("Failed to get command line.\n");
            return false;
        }

        // Just let the engine handle it if there's no command line parameter.
        if (g_cmdline->FindParm("-tickrate") == 0)
        {
            warn("Failed to set tickrate: No \"-tickrate\" command line parameter was passed.\n");
            return false;
        }

        g_desired_tickrate = g_cmdline->ParmValue("-tickrate", 0);
        if (g_desired_tickrate <= 10)
        {
            warn("Failed to set tickrate: \"-tickrate\" command line parameter is too small ({}, <= 10).", g_desired_tickrate);
            return false;
        }

        info("Desired tickrate = {}.\n", g_desired_tickrate);

        // TODO: This might be easier on Linux with `os_get_procedure`.

        u8 *createinterface = (u8 *)gameserver_factory;

        // Check for a jump thunk. It should always be first.
        // Some versions of the game have this for some reason. If it's not found then we don't worry about it.
        for (;;)
        {
            auto thunk_disasm_result = disasm(createinterface);
            if (!thunk_disasm_result.has_value())
            {
                warn("Failed to decode first instruction in \"CreateInterface\".\n");
                return false;
            }

            auto &thunk_disasm = thunk_disasm_result.value();
            if (thunk_disasm.ix.mnemonic != ZYDIS_MNEMONIC_JMP)
            {
                break;
            }

            createinterface = thunk_disasm.ip + thunk_disasm.ix.length + (i32)thunk_disasm.operands[0].imm.value.s;
        }

        InterfaceReg *regs{};

        auto regs_disasm_result = disasm_for_each(
            createinterface,
            ZYDIS_MAX_INSTRUCTION_LENGTH * 20,
            [](auto &&result) noexcept
            {
                // x86-64 is RIP-relative. 32-bit is absolute.
                constexpr ZyanU16       op_size  = TR_ARCH_X86_64 == 1 ? 64 : 32;
                constexpr ZydisRegister mem_base = TR_ARCH_X86_64 == 1 ? ZYDIS_REGISTER_RIP : ZYDIS_REGISTER_NONE;

                return result.ix.mnemonic == ZYDIS_MNEMONIC_MOV && result.ix.operand_count_visible == 2
                    && result.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER && result.operands[0].size == op_size
                    && result.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY && result.operands[1].size == op_size
                    && result.operands[1].mem.segment == ZYDIS_REGISTER_DS && result.operands[1].mem.base == mem_base;
            });
        if (!regs_disasm_result.has_value())
        {
            warn("Failed to find instruction containing \"s_pInterfaceRegs\".\n");
            return false;
        }

        auto &regs_disasm = regs_disasm_result.value();

        // x86-64 is RIP-relative. 32-bit is absolute.
#if TR_ARCH_X86_64
        regs = *(InterfaceReg **)(regs_disasm.ip + regs_disasm.ix.length + (i32)regs_disasm.operands[1].mem.disp.value);
#else
        regs = *(InterfaceReg **)(usize)regs_disasm.operands[1].mem.disp.value;
#endif

        if (regs == nullptr)
        {
            warn("Failed to find \"s_pInterfaceRegs\" (null).\n");
            return false;
        }

        CServerGameDLL *servergame{};

        for (auto *it = regs; it != nullptr; it = it->m_pNext)
        {
            if (!it->m_pName)
            {
                continue;
            }

            if (std::string_view{it->m_pName}.contains("ServerGameDLL"))
            {
                servergame = (CServerGameDLL *)it->m_CreateFn();
                break;
            }
        }

        if (servergame == nullptr)
        {
            warn("Failed to find \"ServerGameDLL\" interface.\n");
            return false;
        }

        g_servergame_hook      = safetyhook::create_vmt(servergame);
        g_GetTickInterval_hook = safetyhook::create_vm(g_servergame_hook, 10, &Hooked_CServerGameDLL::hooked_GetTickInterval);

        info("Loaded.\n");

        return true;
    }

    void Unload() noexcept override
    {
        g_servergame_hook = {};

        info("Unloaded.\n");
    }

    void Pause() noexcept override {}

    void UnPause() noexcept override {}

    const char *GetPluginDescription() noexcept override
    {
        return "CS:S Tickrate (angelfor3v3r)";
    }

    void LevelInit(const char *map_name) noexcept override {}

    void ServerActivate(edict_t *edict_list, i32 edict_count, i32 client_max) noexcept override {}

    void GameFrame(bool simulating) noexcept override {}

    void LevelShutdown() noexcept override {}

    void ClientActive(edict_t *edict) noexcept override {}

    void ClientDisconnect(edict_t *edict) noexcept override {}

    void ClientPutInServer(edict_t *edict, const char *player_name) noexcept override {}

    void SetCommandClient(i32 index) noexcept override {}

    void ClientSettingsChanged(edict_t *edict) noexcept override {}

    PLUGIN_RESULT
    ClientConnect(bool *allow_connect, edict_t *edict, const char *name, const char *address, char *reject, i32 max_reject_len) noexcept override
    {
        return PLUGIN_CONTINUE;
    }

    PLUGIN_RESULT ClientCommand(edict_t *edict, const CCommand &args) noexcept override
    {
        return PLUGIN_CONTINUE;
    }

    PLUGIN_RESULT NetworkIDValidated(const char *user_name, const char *network_id) noexcept override
    {
        return PLUGIN_CONTINUE;
    }

    void OnQueryCvarValueFinished(
        QueryCvarCookie_t cookie, edict_t *edict, EQueryCvarValueStatus status, const char *cvar_name, const char *cvar_value) noexcept override
    {}

    void OnEdictAllocated(edict_t *edict) noexcept override {}

    void OnEdictFreed(edict_t *edict) noexcept override {}

    void FireGameEvent(KeyValues *event) noexcept override {}
};

TickratePlugin g_tickrate_plugin{};

extern "C" TR_DLLEXPORT void *CreateInterface(const char *name, i32 *return_code) noexcept
{
    // First call should be the latest version.
    if (static bool once{}; !once && std::string_view{name}.contains("ISERVERPLUGINCALLBACKS"))
    {
        if (return_code != nullptr)
        {
            *return_code = IFACE_OK;
        }

        once = true;

        return &g_tickrate_plugin;
    }

    if (return_code != nullptr)
    {
        *return_code = IFACE_FAILED;
    }

    return nullptr;
}
