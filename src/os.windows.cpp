#include "os.hpp"
#include "common.hpp"

#if TR_OS_WINDOWS
#include <Windows.h>

[[nodiscard]] u8 *os_get_module(std::string_view module_name) noexcept
{
    return (u8 *)GetModuleHandle(module_name.empty() ? nullptr : module_name.data());
}

[[nodiscard]] u8 *os_get_module(u8 *address) noexcept
{
    HMODULE result;
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)address, &result) == FALSE)
    {
        return nullptr;
    }

    return (u8 *)result;
}

[[nodiscard]] u8 *os_get_procedure(u8 *module, std::string_view proc_name) noexcept
{
    if (module == nullptr || proc_name.empty())
    {
        return nullptr;
    }

    return (u8 *)GetProcAddress((HMODULE)module, proc_name.data());
}
#endif
