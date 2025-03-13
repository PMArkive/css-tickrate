#pragma once

#include <vector>
#include <string>
#include <string_view>

[[nodiscard]] bool                     str_sv_contains(std::string_view str, std::string_view delim) noexcept;
[[nodiscard]] bool                     str_sv_contains(std::string_view str, char delim) noexcept;
[[nodiscard]] std::vector<std::string> str_split(std::string_view str, char delim) noexcept;
