#include "os.hpp"
#include "common.hpp"
#include <scope_guard.hpp>
#include <cstdio>

[[nodiscard]] std::vector<u8> os_read_binary_file(std::string_view path) noexcept
{
    if (path.empty())
    {
        return {};
    }

    // This is unfortunate...
#if TR_OS_WINDOWS
    FILE *file;
    if (fopen_s(&file, path.data(), "rb") != 0)
#else
    auto *file = std::fopen(path.data(), "rb");
    if (file == nullptr)
#endif
    {
        return {};
    }

    auto guard = sg::make_scope_guard([file]() noexcept { std::fclose(file); });

    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        return {};
    }

    auto size = std::ftell(file);
    if (size <= 0)
    {
        return {};
    }

    if (std::fseek(file, 0, SEEK_SET) != 0)
    {
        return {};
    }

    std::vector<u8> buf(size);
    if (std::fread(buf.data(), sizeof(u8), size, file) != (usize)size)
    {
        return {};
    }

    return buf;
}
