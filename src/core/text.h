#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace phos {

std::string to_upper(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool istarts_with(std::string_view s, std::string_view prefix);
std::string_view trim(std::string_view s);
// Splits on runs of spaces/tabs.
std::vector<std::string> split_words(std::string_view s);
// Left-aligns `s` in a field of `width` characters (truncates or pads with spaces).
std::string fit(std::string_view s, std::size_t width);
// Right-aligns `s` in `width` characters.
std::string fit_right(std::string_view s, std::size_t width);

} // namespace phos
