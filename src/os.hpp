#pragma once

#include "type.hpp"
#include <vector>
#include <string>
#include <string_view>

// Reads a binary file to a vector.
[[nodiscard]] std::vector<u8> os_read_binary_file(std::string_view path) noexcept;

// Returns the command line of the running process.
[[nodiscard]] std::string os_get_command_line() noexcept;

// Returns the split command line of the running process.
[[nodiscard]] std::vector<std::string> os_get_split_command_line() noexcept;

// Returns a module handle in the running process.
[[nodiscard]] u8 *os_get_module(std::string_view module_name) noexcept;
[[nodiscard]] u8 *os_get_module(u8 *address) noexcept;

// On Windows: Returns the input module handle.
// On Linux: Returns the base address for a module handle.
[[nodiscard]] u8 *os_get_module_base(u8 *handle) noexcept;

// Returns the full file path for a module.
[[nodiscard]] std::string os_get_module_full_path(u8 *handle) noexcept;

// Find a symbol in a module.
[[nodiscard]] u8 *os_get_procedure(u8 *handle, std::string_view proc_name) noexcept;

[[nodiscard]] inline u8 *os_get_procedure(std::string_view module_name, std::string_view proc_name) noexcept
{
    return os_get_procedure(os_get_module(module_name), proc_name);
}

template <class T>
[[nodiscard]] T os_get_procedure(u8 *handle, std::string_view proc_name) noexcept
{
    return (T)os_get_procedure(handle, proc_name);
}

template <class T>
[[nodiscard]] T os_get_procedure(std::string_view module_name, std::string_view proc_name) noexcept
{
    return (T)os_get_procedure(module_name, proc_name);
}
