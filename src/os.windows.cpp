#include "os.hpp"
#include <Windows.h>

[[nodiscard]] std::string os_get_command_line() noexcept
{
    return GetCommandLine();
}

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

[[nodiscard]] u8 *os_get_module_base(u8 *handle) noexcept
{
    return handle;
}

[[nodiscard]] std::string os_get_module_full_path(u8 *handle) noexcept
{
    if (handle == nullptr)
    {
        return {};
    }

    std::string result(128, '\0');

    for (;;)
    {
        auto len = GetModuleFileName((HMODULE)handle, result.data(), result.size());
        auto err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER)
        {
            result.resize(result.size() * 2);
        }
        else if (err == ERROR_SUCCESS)
        {
            result.resize(len);
            break;
        }
        else
        {
            return {};
        }
    }

    return result;
}

[[nodiscard]] u8 *os_get_procedure(u8 *handle, std::string_view proc_name) noexcept
{
    if (handle == nullptr || proc_name.empty())
    {
        return nullptr;
    }

    return (u8 *)GetProcAddress((HMODULE)handle, proc_name.data());
}
