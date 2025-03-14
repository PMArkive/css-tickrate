#include "string.hpp"

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
