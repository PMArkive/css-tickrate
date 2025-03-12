#include "os.hpp"
#include "common.hpp"

#if TR_OS_LINUX
#include <dlfcn.h>

[[nodiscard]] u8 *os_get_module(std::string_view module_name) noexcept
{
    auto *handle = dlopen(module_name.empty() ? nullptr : module_name.data(), RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr)
    {
        return nullptr;
    }

    dlclose(handle);

    return (u8 *)handle;
}

[[nodiscard]] u8 *os_get_module(u8 *address) noexcept
{
    if (address == nullptr)
    {
		return nullptr;
    }

    Dl_info info;
    if (dladdr(address, &info) == 0)
    {
        return nullptr;
    }

    return os_get_module(info.dli_fname);
}

[[nodiscard]] u8 *os_get_procedure(u8 *module, std::string_view proc_name) noexcept
{
    if (module == nullptr || proc_name.empty())
    {
        return nullptr;
    }

    return (u8 *)dlsym(module, proc_name.data());
}
#endif
