#include "common.hpp"
#include "type.hpp"
#include "os.hpp"
#include "safetyhook/safetyhook.hpp"
#include <tl/expected.hpp>
#include <fmt/format.h>
#include <Zycore/Status.h>
#include <Zydis/Zydis.h>
#include <cstring>
#include <string_view>
#include <array>
#include <charconv>

using CreateInterfaceFn      = void *(TR_CCALL *)(cstr name, i32 *return_code);
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

[[nodiscard]] bool sv_contains(std::string_view str, std::string_view delim) noexcept
{
    return str.find(delim) != std::string_view::npos;
}

template <class T = u8 *>
T get_virtual(const void *object, u16 index) noexcept
{
    return (*(T **)object)[index];
}

[[nodiscard]] std::string_view zyan_status_str(ZyanStatus status) noexcept
{
    // Taken from: https://github.com/zyantific/zydis/blob/v4.1.1/tools/ZydisToolsShared.c#L79-L151
    constexpr std::array<std::string_view, 12> strings_zycore = {/* 00 */ "SUCCESS",
                                                                 /* 01 */ "FAILED",
                                                                 /* 02 */ "TRUE",
                                                                 /* 03 */ "FALSE",
                                                                 /* 04 */ "INVALID_ARGUMENT",
                                                                 /* 05 */ "INVALID_OPERATION",
                                                                 /* 06 */ "NOT_FOUND",
                                                                 /* 07 */ "OUT_OF_RANGE",
                                                                 /* 08 */ "INSUFFICIENT_BUFFER_SIZE",
                                                                 /* 09 */ "NOT_ENOUGH_MEMORY",
                                                                 /* 0A */ "NOT_ENOUGH_MEMORY",
                                                                 /* 0B */ "BAD_SYSTEMCALL"};

    constexpr std::array<std::string_view, 13> strings_zydis = {/* 00 */ "NO_MORE_DATA",
                                                                /* 01 */ "DECODING_ERROR",
                                                                /* 02 */ "INSTRUCTION_TOO_LONG",
                                                                /* 03 */ "BAD_REGISTER",
                                                                /* 04 */ "ILLEGAL_LOCK",
                                                                /* 05 */ "ILLEGAL_LEGACY_PFX",
                                                                /* 06 */ "ILLEGAL_REX",
                                                                /* 07 */ "INVALID_MAP",
                                                                /* 08 */ "MALFORMED_EVEX",
                                                                /* 09 */ "MALFORMED_MVEX",
                                                                /* 0A */ "INVALID_MASK",
                                                                /* 0B */ "SKIP_TOKEN",
                                                                /* 0C */ "IMPOSSIBLE_INSTRUCTION"};

    // if (ZYAN_STATUS_MODULE(status) >= ZYAN_MODULE_USER)
    // {
    //     return "User";
    // }

    if (ZYAN_STATUS_MODULE(status) == ZYAN_MODULE_ZYCORE)
    {
        status = ZYAN_STATUS_CODE(status);
        return status < strings_zycore.size() ? strings_zycore[status] : "";
    }

    if (ZYAN_STATUS_MODULE(status) == ZYAN_MODULE_ZYDIS)
    {
        status = ZYAN_STATUS_CODE(status);
        return status < strings_zydis.size() ? strings_zydis[status] : "";
    }

    return "";
}

struct Disasm
{
    struct Error
    {
        u8        *ip{};
        ZyanStatus status{ZYAN_STATUS_FAILED};

        [[nodiscard]] auto status_str() const noexcept
        {
            return zyan_status_str(status);
        }
    };

    u8                     *ip{};
    ZydisDecodedInstruction ix{};
    ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT]{};
};

[[nodiscard]] tl::expected<Disasm, Disasm::Error> disasm(u8 *ip, usize len = ZYDIS_MAX_INSTRUCTION_LENGTH) noexcept
{
    static ZydisDecoder decoder;

    if (static bool once{}; !once)
    {
#if TR_ARCH_X86_64
        auto status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
#else
        auto status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
#endif
        if (ZYAN_SUCCESS(status) == ZYAN_FALSE)
        {
            return tl::unexpected{Disasm::Error{ip, status}};
        }

        once = true;
    }

    Disasm result{};

    auto status = ZydisDecoderDecodeFull(&decoder, ip, len, &result.ix, result.operands);
    if (ZYAN_SUCCESS(status) == ZYAN_FALSE)
    {
        return tl::unexpected{Disasm::Error{ip, status}};
    }

    result.ip = ip;

    return result;
}

template <class Pred>
tl::expected<Disasm, Disasm::Error> disasm_for_each(u8 *ip, usize len, Pred &&pred) noexcept
{
    u8 *end = ip + len;

    for (u8 *i = ip; i < end;)
    {
        auto result = disasm(i, (usize)(end - i));
        if (!result.has_value())
        {
            return tl::unexpected{result.error()};
        }

        auto &&value = result.value();
        if (pred(value))
        {
            return value;
        }

        i += value.ix.length;
    }

    // This means we've scanned everything successfully but the predicate didn't return.
    return tl::unexpected{Disasm::Error{ip}};
}

class InterfaceReg
{
public:
    InstantiateInterfaceFn m_CreateFn;
    cstr                   m_pName;
    InterfaceReg          *m_pNext;
};

class CCommandLine
{
public:
    [[nodiscard]] cstr CheckParm(cstr name, cstr *out_value = nullptr) const noexcept
    {
        return get_virtual<cstr(TR_THISCALL *)(decltype(this), cstr, cstr *)>(this, 3)(this, name, out_value);
    }
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

using Warning_fn = void(TR_CCALL *)(cstr, ...);
using Msg_fn     = void(TR_CCALL *)(cstr, ...);

// Global variables.
Warning_fn       g_Warning{};
Msg_fn           g_Msg{};
CCommandLine    *g_cmdline{};
i32              g_desired_tickrate{};
SafetyHookInline g_GetTickInterval_hook{};

template <class... Args>
void warn(std::string_view fmt_str, Args &&...args) noexcept
{
    g_Warning("[Tickrate] [warn] %s", fmt::vformat(fmt_str, fmt::make_format_args(args...)).c_str());
}

template <class... Args>
void info(std::string_view fmt_str, Args &&...args) noexcept
{
    g_Msg("[Tickrate] [info] %s", fmt::vformat(fmt_str, fmt::make_format_args(args...)).c_str());
}

f32 TR_THISCALL hooked_CServerGameDLL_GetTickInterval([[maybe_unused]] CServerGameDLL *instance) noexcept
{
    f32 interval = 1.0f / (f32)g_desired_tickrate;

    return interval;
}

class TickratePlugin final : public IServerPluginCallbacks,
                             public IGameEventListener
{
public:
    TickratePlugin() noexcept           = default;
    ~TickratePlugin() noexcept override = default;

    bool Load(CreateInterfaceFn interface_factory, CreateInterfaceFn gameserver_factory) noexcept override
    {
        constexpr std::string_view tier0_lib_name = TR_OS_WINDOWS == 1 ? "tier0.dll" : "libtier0_srv.so";

        u8 *tier0_lib = os_get_module(tier0_lib_name);
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
            warn("Failed to find `Msg` procedure.\n");
            return false;
        }

        info("Loading...\n");

        auto *CommandLine_Tier0 = os_get_procedure<CCommandLine *(TR_CCALL *)()>(tier0_lib, "CommandLine_Tier0");
        if (CommandLine_Tier0 == nullptr)
        {
            warn("Failed to find `CommandLine_Tier0` procedure.\n");
            return false;
        }

        g_cmdline = CommandLine_Tier0();
        if (g_cmdline == nullptr)
        {
            warn("Failed to get command line.\n");
            return false;
        }

        // Parse command line... or just let the engine handle it if there's no `-tickrate` parameter.
        cstr  tickrate_str{};
        auto *tickrate = g_cmdline->CheckParm("-tickrate", &tickrate_str);
        if (tickrate == nullptr || tickrate_str == nullptr)
        {
            warn("Failed to set tickrate: No `-tickrate` command line parameter was passed.\n");
            return false;
        }

        auto [ptr, ec] = std::from_chars(tickrate_str, tickrate_str + std::strlen(tickrate_str), g_desired_tickrate);
        if (ec != std::errc{})
        {
            warn("Failed to convert `-tickrate` command line parameter.\n");
            return false;
        }

        if (g_desired_tickrate <= 10)
        {
            warn(
                "Failed to set tickrate: `-tickrate` command line parameter is too low (Desired tickrate is {}, minimum is 11).\n",
                g_desired_tickrate);
            return false;
        }

        info("Desired tickrate is {}.\n", g_desired_tickrate);

        // TODO: This might be easier on Linux with `os_get_procedure`.

        u8 *createinterface = (u8 *)gameserver_factory;

        // Check for a jump thunk. It should always be first.
        // Some versions of the game have this for some reason. If there isn't one then we don't worry about it.
        for (;;)
        {
            auto thunk_disasm_result = disasm(createinterface);
            if (!thunk_disasm_result.has_value())
            {
                warn("Failed to decode first instruction in `CreateInterface`: {}\n", thunk_disasm_result.error().status_str());
                return false;
            }

            auto &&thunk_disasm = thunk_disasm_result.value();
            if (thunk_disasm.ix.mnemonic != ZYDIS_MNEMONIC_JMP)
            {
                break;
            }

            createinterface += thunk_disasm.ix.length + (i32)thunk_disasm.operands[0].imm.value.s;
        }

        auto regs_disasm_result = disasm_for_each(
            createinterface,
            ZYDIS_MAX_INSTRUCTION_LENGTH * 25,
            [](auto &&result) noexcept
            {
                // x86-64 is RIP-relative. x86-32 is absolute.
                constexpr ZyanU16       op_size  = TR_ARCH_X86_64 == 1 ? 64 : 32;
                constexpr ZydisRegister mem_base = TR_ARCH_X86_64 == 1 ? ZYDIS_REGISTER_RIP : ZYDIS_REGISTER_NONE;

                return result.ix.mnemonic == ZYDIS_MNEMONIC_MOV && result.ix.operand_count_visible == 2
                    && result.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER && result.operands[0].size == op_size
                    && result.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY && result.operands[1].size == op_size
                    && result.operands[1].mem.segment == ZYDIS_REGISTER_DS && result.operands[1].mem.base == mem_base;
            });
        if (!regs_disasm_result.has_value())
        {
            warn("Failed to find instruction containing `s_pInterfaceRegs`: {}\n", regs_disasm_result.error().status_str());
            return false;
        }

        auto &&regs_disasm = regs_disasm_result.value();

        // x86-64 is RIP-relative. x86-32 is absolute.
#if TR_ARCH_X86_64
        auto *regs = *(InterfaceReg **)(regs_disasm.ip + regs_disasm.ix.length + (i32)regs_disasm.operands[1].mem.disp.value);
#else
        auto *regs = *(InterfaceReg **)(usize)regs_disasm.operands[1].mem.disp.value;
#endif

        if (regs == nullptr)
        {
            warn("Failed to find `s_pInterfaceRegs` (null).\n");
            return false;
        }

        CServerGameDLL *servergame{};

        for (auto *it = regs; it != nullptr; it = it->m_pNext)
        {
            if (it->m_pName == nullptr)
            {
                continue;
            }

            if (sv_contains(it->m_pName, "ServerGameDLL"))
            {
                servergame = (CServerGameDLL *)it->m_CreateFn();
                break;
            }
        }

        if (servergame == nullptr)
        {
            warn("Failed to find `ServerGameDLL` interface.\n");
            return false;
        }

        g_GetTickInterval_hook = safetyhook::create_inline(get_virtual(servergame, 10), hooked_CServerGameDLL_GetTickInterval);
        if (!g_GetTickInterval_hook)
        {
            warn("Failed to hook `CServerGameDLL::GetTickInterval` function.\n");
            return false;
        }

        info("Loaded.\n");

        return true;
    }

    void Unload() noexcept override
    {
        g_GetTickInterval_hook = {};

        info("Unloaded.\n");
    }

    void Pause() noexcept override {}

    void UnPause() noexcept override {}

    cstr GetPluginDescription() noexcept override
    {
        return "Tickrate (angelfor3v3r)";
    }

    void LevelInit(cstr map_name) noexcept override {}

    void ServerActivate(edict_t *edict_list, i32 edict_count, i32 client_max) noexcept override {}

    void GameFrame(bool simulating) noexcept override {}

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
    if (sv_contains(name, "ISERVERPLUGINCALLBACKS"))
    {
        result = &g_tickrate_plugin;
    }

    if (return_code != nullptr)
    {
        *return_code = result != nullptr ? IFACE_OK : IFACE_FAILED;
    }

    return result;
}
