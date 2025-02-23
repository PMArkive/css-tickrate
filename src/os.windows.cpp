#include "os.hpp"
#include "common.hpp"

#if TR_OS_WINDOWS
#include <Windows.h>

[[nodiscard]] u8 *os_get_module(std::string_view module_name) noexcept
{
    return (u8 *)GetModuleHandle(module_name.empty() ? nullptr : module_name.data());
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
