#include "utl.hpp"
#include "common.hpp"
#include <cctype>
#include <array>

namespace utl
{
    [[nodiscard]] std::string_view Disasm::Error::status_str() const noexcept
    {
        return zyan_status_str(status);
    }

    [[nodiscard]] bool sv_contains(std::string_view str, std::string_view delim) noexcept
    {
        return str.find(delim) != std::string_view::npos;
    }

    [[nodiscard]] bool sv_contains(std::string_view str, char delim) noexcept
    {
        return str.find(delim) != std::string_view::npos;
    }

    void ltrim(std::string &str) noexcept
    {
        ltrim(str, [](u8 ch) noexcept { return std::isspace(ch) == 0; });
    }

    void rtrim(std::string &str) noexcept
    {
        rtrim(str, [](u8 ch) noexcept { return std::isspace(ch) == 0; });
    }

    void trim(std::string &str) noexcept
    {
        ltrim(str);
        rtrim(str);
    }

    [[nodiscard]] std::vector<std::string> split(std::string_view str, char delim) noexcept
    {
        std::vector<std::string>    result{};
        std::string_view::size_type i, last{};

        while ((i = str.find(delim, last)) != std::string_view::npos)
        {
            result.emplace_back(str.substr(last, i - last));
            last = i + 1;
        }

        result.emplace_back(str.substr(last));

        return result;
    }

    [[nodiscard]] std::string_view zyan_status_str(ZyanStatus status) noexcept
    {
        // Taken from: https://github.com/zyantific/zydis/blob/v4.1.1/tools/ZydisToolsShared.c#L79-L151
        constexpr std::array<std::string_view, 12> strings_zycore = {/* 00 */ "SUCCESS",
                                                                     /* 01 */ "FAILED",
                                                                     /* 02 */ "TRUE",
                                                                     /* 03 */ "FALSE",
                                                                     /* 04 */ "INVALID_ARGUMENT",
                                                                     /* 05 */ "INVALID_OPERATION",
                                                                     /* 06 */ "NOT_FOUND",
                                                                     /* 07 */ "OUT_OF_RANGE",
                                                                     /* 08 */ "INSUFFICIENT_BUFFER_SIZE",
                                                                     /* 09 */ "NOT_ENOUGH_MEMORY",
                                                                     /* 0A */ "NOT_ENOUGH_MEMORY",
                                                                     /* 0B */ "BAD_SYSTEMCALL"};

        constexpr std::array<std::string_view, 13> strings_zydis = {/* 00 */ "NO_MORE_DATA",
                                                                    /* 01 */ "DECODING_ERROR",
                                                                    /* 02 */ "INSTRUCTION_TOO_LONG",
                                                                    /* 03 */ "BAD_REGISTER",
                                                                    /* 04 */ "ILLEGAL_LOCK",
                                                                    /* 05 */ "ILLEGAL_LEGACY_PFX",
                                                                    /* 06 */ "ILLEGAL_REX",
                                                                    /* 07 */ "INVALID_MAP",
                                                                    /* 08 */ "MALFORMED_EVEX",
                                                                    /* 09 */ "MALFORMED_MVEX",
                                                                    /* 0A */ "INVALID_MASK",
                                                                    /* 0B */ "SKIP_TOKEN",
                                                                    /* 0C */ "IMPOSSIBLE_INSTRUCTION"};

        // if (ZYAN_STATUS_MODULE(status) >= ZYAN_MODULE_USER)
        // {
        //     return "User";
        // }

        if (ZYAN_STATUS_MODULE(status) == ZYAN_MODULE_ZYCORE)
        {
            status = ZYAN_STATUS_CODE(status);
            return status < strings_zycore.size() ? strings_zycore[status] : "";
        }

        if (ZYAN_STATUS_MODULE(status) == ZYAN_MODULE_ZYDIS)
        {
            status = ZYAN_STATUS_CODE(status);
            return status < strings_zydis.size() ? strings_zydis[status] : "";
        }

        return {};
    }

    [[nodiscard]] std::string_view safetyhookinline_error_str(const SafetyHookInline::Error &error) noexcept
    {
        constexpr std::array<std::string_view, 7> strings_inline_hook = {
            "BAD_ALLOCATION",
            "FAILED_TO_DECODE_INSTRUCTION",
            "SHORT_JUMP_IN_TRAMPOLINE",
            "IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE",
            "UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE",
            "FAILED_TO_UNPROTECT",
            "NOT_ENOUGH_SPACE",
        };

        return strings_inline_hook[error.type];
    }

    [[nodiscard]] tl::expected<Disasm, Disasm::Error> disasm(u8 *ip, usize len) noexcept
    {
        static ZydisDecoder decoder;
        if (static bool once{}; !once)
        {
#if TR_ARCH_X86_64
            auto status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
#else
            auto status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
#endif
            if (ZYAN_SUCCESS(status) == ZYAN_FALSE)
            {
                return tl::unexpected{Disasm::Error{ip, status}};
            }

            once = true;
        }

        Disasm result{};
        auto   status = ZydisDecoderDecodeFull(&decoder, ip, len, &result.ix, result.operands);
        if (ZYAN_SUCCESS(status) == ZYAN_FALSE)
        {
            return tl::unexpected{Disasm::Error{ip, status}};
        }

        result.ip = ip;

        return result;
    }
} // namespace utl
