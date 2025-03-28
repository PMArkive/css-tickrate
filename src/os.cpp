#include "os.hpp"
#include "common.hpp"
#include <scope_guard.hpp>
#include <cstdio>
#include <array>

[[nodiscard]] std::vector<u8> os_read_binary_file(std::string_view path) noexcept
{
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

    // Try to get the file size first.
    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        return {};
    }

    auto file_size = std::ftell(file);

    if (std::fseek(file, 0, SEEK_SET) != 0)
    {
        return {};
    }

    // We have a size, read it all in one go.
    if (file_size > 0)
    {
        std::vector<u8> result(file_size);
        if (std::fread(result.data(), 1, file_size, file) != (usize)file_size)
        {
            return {};
        }

        return result;
    }

    // Size is zero... try to read in chunks.
    constexpr usize chunk_size = 4096;
    std::vector<u8> result{};

    for (;;)
    {
        std::array<u8, chunk_size> tmp;
        if (auto read_amount = std::fread(tmp.data(), 1, chunk_size, file); read_amount > 0)
        {
            result.insert(result.end(), tmp.begin(), tmp.begin() + (decltype(tmp)::difference_type)read_amount);
        }

        // Read error.
        if (std::ferror(file) != 0)
        {
            return {};
        }
        // Reached end of file.
        else if (std::feof(file) != 0)
        {
            break;
        }
    }

    return result;
}
