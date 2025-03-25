#include "common.hpp"
#include "type.hpp"
#include "utl.hpp"
#include "os.hpp"
#include "game.hpp"
#include "lua/lua_loader.hpp"
#include <tl/expected.hpp>
#include <fmt/format.h>
#include <safetyhook/safetyhook.hpp>
#include <cctype>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <map>
#include <charconv>
#include <filesystem>

// Global variables, etc.
LuaScriptLoader  g_lua_loader{};
u16              g_desired_tickrate{};
SafetyHookInline g_GetTickInterval_hook{};

// Misc utils.
[[nodiscard]] tl::expected<InterfaceReg *, std::string> find_regs(u8 *module_handle) noexcept
{
    // Name used for error printing.
    auto module_name = os_get_module_full_path(module_handle);
    if (module_name.empty())
    {
        return tl::unexpected{"Invalid module name"};
    }

    module_name = std::filesystem::path{module_name}.filename().string();

    // Check for the `s_pInterfaceRegs` symbol first.
    if (u8 *regs_symbol = os_get_procedure(module_handle, "s_pInterfaceRegs"); regs_symbol != nullptr)
    {
        return *(InterfaceReg **)regs_symbol;
    }

    // No symbol was found so we have to disasm manually.
    u8 *create_interface = os_get_procedure(module_handle, "CreateInterface");
    if (create_interface == nullptr)
    {
        return tl::unexpected{fmt::format("Failed to find `{}!CreateInterface`", module_name)};
    }

    // First we check for a jump thunk. Some versions of the game have this for some reason. If there isn't one then we don't worry about it.
    for (;;)
    {
        auto thunk_disasm_result = utl::disasm(create_interface);
        if (!thunk_disasm_result)
        {
            return tl::unexpected{
                fmt::format("Failed to decode first instruction in `{}!CreateInterface`: {}", module_name, thunk_disasm_result.error().status_str())};
        }

        auto &thunk_disasm = *thunk_disasm_result;
        if (thunk_disasm.ix.mnemonic != ZYDIS_MNEMONIC_JMP)
        {
            break;
        }

        create_interface += thunk_disasm.ix.length + (i32)thunk_disasm.operands[0].imm.value.s;
    }

    // Find the first `mov reg, mem`.
    auto regs_disasm_result = utl::disasm_for_each(
        create_interface,
        ZYDIS_MAX_INSTRUCTION_LENGTH * 25, // I hope this is enough :P
        [](auto &result) noexcept
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
        return tl::unexpected{
            fmt::format("Failed to find instruction containing `{}!s_pInterfaceRegs`: {}", module_name, regs_disasm_result.error().status_str())};
    }

    auto &regs_disasm = *regs_disasm_result;

    // x86-64 is RIP-relative. x86-32 is absolute.
#if TR_ARCH_X86_64
    return *(InterfaceReg **)(regs_disasm.ip + regs_disasm.ix.length + (i32)regs_disasm.operands[1].mem.disp.value);
#else
    return *(InterfaceReg **)((usize)regs_disasm.operands[1].mem.disp.value);
#endif
}

// Dummy function so we can find our own module.
void find_me() noexcept {}

// Hooks.
class Hooked_CServerGameDLL : public CServerGameDLL
{
public:
    static f32 TR_THISCALL hooked_GetTickInterval([[maybe_unused]] CServerGameDLL *instance) noexcept
    {
        f32 interval = 1.0f / (f32)g_desired_tickrate;

        return interval;
    }
};

// Plugin.
class TickratePlugin final : public IServerPluginCallbacks,
                             public IGameEventListener
{
public:
    TickratePlugin() noexcept           = default;
    ~TickratePlugin() noexcept override = default;

    bool Load(CreateInterfaceFn interface_factory, CreateInterfaceFn gameserver_factory) noexcept override
    {
        utl::print_info("Loading...");

        // Find our own module.
        u8 *our_module = os_get_module((u8 *)find_me);
        if (our_module == nullptr)
        {
            utl::print_error("Failed to get our own module.");
            return false;
        }

        auto our_module_full_path = os_get_module_full_path(our_module);
        if (our_module_full_path.empty())
        {
            utl::print_error("Failed to get our own module's full path.");
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
                utl::print_error("Failed to create autorun directory.");
                return false;
            }
        }

        u8 *server_module = os_get_module((u8 *)gameserver_factory);
        if (server_module == nullptr)
        {
            utl::print_error("Failed to get server module.");
            return false;
        }

        auto cmdline = os_get_split_command_line();
        if (cmdline.empty())
        {
            utl::print_error("Failed to get command line.");
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

        utl::print_info("mod = {}", mod_value);

        if (tickrate_value.empty())
        {
            utl::print_error("Bad tickrate: Failed to find `-tickrate` command line string.");
            return false;
        }

        auto [ptr, ec] = std::from_chars(tickrate_value.data(), tickrate_value.data() + tickrate_value.size(), g_desired_tickrate);
        if (ec != std::errc{})
        {
            utl::print_error("Bad tickrate: Failed to convert `-tickrate` command line value.");
            return false;
        }

        // This is not a bug. They're swapped for a reason (we convert them to an int instead of comparing the float).
        constexpr u16 min_tickrate = (u16)(1.0f / MAXIMUM_TICK_INTERVAL) + 1;
        constexpr u16 max_tickrate = (u16)(1.0f / MINIMUM_TICK_INTERVAL) + 1;

        if (g_desired_tickrate < min_tickrate)
        {
            utl::print_error(
                "Bad tickrate: `-tickrate` command line value is too low (Desired tickrate is {}, minimum is {}). Server will continue with default "
                "tickrate.",
                g_desired_tickrate,
                min_tickrate);
            return false;
        }
        else if (g_desired_tickrate > max_tickrate)
        {
            utl::print_error(
                "Bad tickrate: `-tickrate` command line value is too high (Desired tickrate is {}, maximum is {}). Server will continue with default "
                "tickrate.",
                g_desired_tickrate,
                max_tickrate);
            return false;
        }

        utl::print_info("Desired tickrate is {}.", g_desired_tickrate);

        // Find interfaces in the server module.
        auto server_regs_result = find_regs(server_module);
        if (!server_regs_result)
        {
            utl::print_error("Server regs error: {}.", server_regs_result.error());
            return false;
        }

        auto find_interface = [](InterfaceReg *start, std::string_view name) noexcept -> u8 *
        {
            static std::unordered_map<InterfaceReg *, std::map<std::string, u8 *, std::greater<>>> cache{};

            if (cache.find(start) == cache.end())
            {
                for (auto *it = start; it != nullptr; it = it->m_pNext)
                {
                    cache[start][it->m_pName] = (u8 *)it->m_CreateFn();
                }
            }

            for (auto &&[base_regs, interfaces] : cache)
            {
                for (auto &&[interface_name, interface_ptr] : interfaces)
                {
                    // Get rid of trailing numbers.
                    std::string cur_name = interface_name;
                    utl::rtrim(cur_name, [](u8 ch) noexcept { return std::isdigit(ch) == 0; });

                    if (cur_name == name)
                    {
                        return interface_ptr;
                    }
                }
            }

            return nullptr;
        };

        auto *server_game = (CServerGameDLL *)find_interface(*server_regs_result, "ServerGameDLL");
        if (server_game == nullptr)
        {
            utl::print_error("Failed to find `ServerGameDLL` interface.");
            return false;
        }

        auto *player_info_manager = (CPlayerInfoManager *)find_interface(*server_regs_result, "PlayerInfoManager");
        if (player_info_manager == nullptr)
        {
            utl::print_error("Failed to find `PlayerInfoManager` interface.");
            return false;
        }

        auto *globals = player_info_manager->GetGlobalVars();
        if (globals == nullptr)
        {
            utl::print_error("Failed to find `CGlobalVars`.");
            return false;
        }

        u8 *engine_module = os_get_module((u8 *)globals);
        if (engine_module == nullptr)
        {
            utl::print_error("Failed to get engine module.");
            return false;
        }

        auto engine_regs_result = find_regs(engine_module);
        if (!engine_regs_result)
        {
            utl::print_error("Engine regs error: {}.", engine_regs_result.error());
            return false;
        }

        auto *engine = (CVEngineServer *)find_interface(*engine_regs_result, "VEngineServer");
        if (engine == nullptr)
        {
            utl::print_error("Failed to find `VEngineServer` interface.");
            return false;
        }

        g_game.mod_name = mod_value;
        g_game.globals  = globals;
        g_game.engine   = engine;

        utl::print_info("Applying hooks...");

        // TODO: There's a chance that this virtual function might not always be index 10. Will need testing.
        u8 *fn = utl::get_virtual(server_game, 10);

        // TODO: Switch to global VMT hooks when it's available.
        auto hook_result = safetyhook::InlineHook::create(fn, Hooked_CServerGameDLL::hooked_GetTickInterval);
        if (!hook_result)
        {
            auto &err = hook_result.error();
            utl::print_error(
                "Failed to hook `CServerGameDLL::GetTickInterval` function: {} @ 0x{:X}", utl::safetyhookinline_error_str(err), (usize)err.ip);

            return false;
        }

        g_GetTickInterval_hook = std::move(*hook_result);

        // Set up Lua.
        if (!g_lua_loader.init(autorun_dir))
        {
            utl::print_error("Failed to initialize Lua loader.");
            return false;
        }

        g_lua_loader.on_load();

        utl::print_info("Loaded!");

        return true;
    }

    void Unload() noexcept override
    {
        // Notify scripts of unload.
        g_lua_loader.reset_scripts();

        g_GetTickInterval_hook = {};

        utl::print_info("Unloaded.");
    }

    void Pause() noexcept override {}

    void UnPause() noexcept override {}

    cstr GetPluginDescription() noexcept override
    {
        return "Tickrate (angelfor3v3r)";
    }

    void LevelInit(cstr map_name) noexcept override
    {
        g_lua_loader.on_level_init(map_name);
    }

    void ServerActivate(edict_t *edict_list, i32 edict_count, i32 client_max) noexcept override {}

    void GameFrame(bool simulating) noexcept override
    {
        g_lua_loader.on_game_frame(simulating);
    }

    void LevelShutdown() noexcept override
    {
        g_lua_loader.on_level_shutdown();
    }

    void ClientActive(edict_t *edict) noexcept override {}

    void ClientDisconnect(edict_t *edict) noexcept override
    {
        g_lua_loader.on_client_disconnect(edict);
    }

    void ClientPutInServer(edict_t *edict, cstr player_name) noexcept override
    {
        g_lua_loader.on_client_spawn(edict, player_name);
    }

    void SetCommandClient(i32 index) noexcept override {}

    void ClientSettingsChanged(edict_t *edict) noexcept override {}

    PLUGIN_RESULT
    ClientConnect(bool *allow_connect, edict_t *edict, cstr name, cstr address, char *reject, i32 max_reject_len) noexcept override
    {
        auto lua_result = g_lua_loader.on_client_connect(allow_connect, edict, name, address, reject, max_reject_len);

        return lua_result;
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
