#include "os.hpp"

#if TR_OS_WINDOWS
[[nodiscard]] ModuleHandle os_get_module(std::string_view module_name) noexcept
{
    return GetModuleHandle(module_name.empty() ? nullptr : module_name.data());
}

[[nodiscard]] u8 *os_get_procedure(ModuleHandle module, std::string_view proc_name) noexcept
{
    if (module == nullptr || proc_name.empty())
    {
        return nullptr;
    }

    return (u8 *)GetProcAddress(module, proc_name.data());
}
#endif
