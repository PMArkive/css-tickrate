#pragma once

#include "common.hpp"
#include "type.hpp"
#include <string_view>

#if TR_OS_WINDOWS
#include <Windows.h>
using ModuleHandle = HMODULE;
#else
using ModuleHandle = void *;
#endif

[[nodiscard]] ModuleHandle os_get_module(std::string_view module_name) noexcept;
[[nodiscard]] u8          *os_get_procedure(ModuleHandle module, std::string_view proc_name) noexcept;

[[nodiscard]] inline u8 *os_get_procedure(std::string_view module_name, std::string_view proc_name) noexcept
{
    return os_get_procedure(os_get_module(module_name), proc_name);
}

template <class T>
[[nodiscard]] T os_get_procedure(ModuleHandle module, std::string_view proc_name) noexcept
{
    return (T)os_get_procedure(module, proc_name);
}

template <class T>
[[nodiscard]] T os_get_procedure(std::string_view module_name, std::string_view proc_name) noexcept
{
    return (T)os_get_procedure(module_name, proc_name);
}
