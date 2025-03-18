#include "os.hpp"
#include <link.h>
#include <dlfcn.h>

[[nodiscard]] std::string os_get_command_line() noexcept
{
    auto buf = os_read_binary_file("/proc/self/cmdline");
    if (buf.empty())
    {
        return {};
    }

    return {(cstr)buf.data(), buf.size()};
}

[[nodiscard]] u8 *os_get_module(std::string_view module_name) noexcept
{
    auto *result = dlopen(module_name.empty() ? nullptr : module_name.data(), RTLD_NOW | RTLD_NOLOAD);
    if (result == nullptr)
    {
        return nullptr;
    }

    dlclose(result);

    return (u8 *)result;
}

[[nodiscard]] u8 *os_get_module(u8 *address) noexcept
{
    if (address == nullptr)
    {
        return nullptr;
    }

    Dl_info info;
    if (dladdr(address, &info) == 0 || info.dli_fname == nullptr)
    {
        return nullptr;
    }

    return os_get_module(info.dli_fname);
}

[[nodiscard]] u8 *os_get_module_base(u8 *handle) noexcept
{
    if (handle == nullptr)
    {
        return nullptr;
    }

    link_map link;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &link) != 0)
    {
        return nullptr;
    }

    return (u8 *)link.l_addr;
}

[[nodiscard]] u8 *os_get_procedure(u8 *handle, std::string_view proc_name) noexcept
{
    if (handle == nullptr || proc_name.empty())
    {
        return nullptr;
    }

    return (u8 *)dlsym(handle, proc_name.data());
}
