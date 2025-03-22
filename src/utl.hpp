#pragma once

#include "type.hpp"
#include <tl/expected.hpp>
#include <fmt/format.h>
#include <Zycore/Status.h>
#include <Zydis/Zydis.h>
#include <safetyhook/safetyhook.hpp>
#include <utility>
#include <vector>
#include <string>
#include <string_view>
#include <cstdio>

namespace utl
{
    struct Disasm
    {
        struct Error
        {
            u8        *ip{};
            ZyanStatus status{ZYAN_STATUS_FAILED};

            [[nodiscard]] std::string_view status_str() const noexcept;
        };

        u8                     *ip{};
        ZydisDecodedInstruction ix;
        ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT];
    };

    // Returns true if the input string contains the delimiter.
    [[nodiscard]] bool sv_contains(std::string_view str, std::string_view delim) noexcept;
    [[nodiscard]] bool sv_contains(std::string_view str, char delim) noexcept;

    // Trim whitespace from the left side of a string.
    void ltrim(std::string &str) noexcept;

    // Trim whitespace from the right side of a string.
    void rtrim(std::string &str) noexcept;

    // Trim whitespace from the both sides of a string.
    void trim(std::string &str) noexcept;

    // Split a string by a delimiter.
    [[nodiscard]] std::vector<std::string> split(std::string_view str, char delim) noexcept;

    // Returns an error string for a `ZyanStatus`.
    [[nodiscard]] std::string_view zyan_status_str(ZyanStatus status) noexcept;

    // Returns an error string for a `SafetyHookInline::Error`.
    [[nodiscard]] std::string_view safetyhookinline_error_str(const SafetyHookInline::Error &error) noexcept;

    // Disassemble a single x86 instruction.
    [[nodiscard]] tl::expected<Disasm, Disasm::Error> disasm(u8 *ip, usize len = ZYDIS_MAX_INSTRUCTION_LENGTH) noexcept;

    // Print a formatted message to `stdout`.
    template <class... Args>
    void print_info(fmt::format_string<Args...> fmt, Args &&...args) noexcept
    {
        std::fprintf(stdout, "[Tickrate] [info] %s\n", fmt::format(fmt, std::forward<Args>(args)...).c_str());
        std::fflush(stdout);
    }

    // Print a formatted error to `stderr`.
    template <class... Args>
    void print_error(fmt::format_string<Args...> fmt, Args &&...args) noexcept
    {
        std::fprintf(stderr, "[Tickrate] [error] %s\n", fmt::format(fmt, std::forward<Args>(args)...).c_str());
        std::fflush(stderr);
    }

    // Returns a virtual function from a virtual method table by its index.
    template <class T = u8 *>
    T get_virtual(const void *object, u16 index) noexcept
    {
        return (*(T **)object)[index];
    }

    template <class Pred>
    tl::expected<Disasm, Disasm::Error> disasm_for_each(u8 *ip, usize len, Pred &&pred) noexcept
    {
        u8 *end = ip + len;

        for (u8 *i = ip; i < end;)
        {
            auto result = disasm(i, end - i);
            if (!result)
            {
                return tl::unexpected{result.error()};
            }

            auto &&value = *result;
            if (pred(value))
            {
                return value;
            }

            i += value.ix.length;
        }

        // This means we've scanned everything successfully but the predicate didn't return.
        return tl::unexpected{Disasm::Error{ip}};
    }
} // namespace utl
