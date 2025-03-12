#pragma once

#include "type.hpp"
#include <string_view>

[[nodiscard]] u8 *os_get_module(std::string_view module_name) noexcept;
[[nodiscard]] u8 *os_get_module(u8 *address) noexcept;
[[nodiscard]] u8 *os_get_procedure(u8 *module, std::string_view proc_name) noexcept;

[[nodiscard]] inline u8 *os_get_procedure(std::string_view module_name, std::string_view proc_name) noexcept
{
    return os_get_procedure(os_get_module(module_name), proc_name);
}

template <class T>
[[nodiscard]] T os_get_procedure(u8 *module, std::string_view proc_name) noexcept
{
    return (T)os_get_procedure(module, proc_name);
}

template <class T>
[[nodiscard]] T os_get_procedure(std::string_view module_name, std::string_view proc_name) noexcept
{
    return (T)os_get_procedure(module_name, proc_name);
}
