#include "string.hpp"
#include "type.hpp"

[[nodiscard]] bool str_sv_contains(std::string_view str, std::string_view delim) noexcept
{
    return str.find(delim) != std::string_view::npos;
}

[[nodiscard]] bool str_sv_contains(std::string_view str, char delim) noexcept
{
    return str.find(delim) != std::string_view::npos;
}

[[nodiscard]] std::vector<std::string> str_split(std::string_view str, char delim) noexcept
{
    std::vector<std::string> result{};
    usize                    last{};
    for (auto i = str.find(delim); i != std::string::npos; last = i + 1, i = str.find(delim, i + 1))
    {
        result.emplace_back(str.substr(last, i - last));
    }

    result.emplace_back(str.substr(last));

    return result;
}
