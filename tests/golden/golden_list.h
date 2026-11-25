#pragma once

#include <array>
#include <string_view>

namespace phos::test {

struct GoldenPage {
    std::string_view page;
    std::string_view theme;
    std::string_view file;
};

// Every page implemented so far, plus the index in two more themes to cover palette resolution.
inline constexpr std::array<GoldenPage, 6> kGoldenPages = {{
    {"1", "GREEN", "page-1.png"},
    {"S", "GREEN", "page-s.png"},
    {"H", "GREEN", "page-h.png"},
    {"TESTCARD", "GREEN", "testcard.png"},
    {"1", "AMBER", "page-1-amber.png"},
    {"1", "VAPOR", "page-1-vapor.png"},
}};

} // namespace phos::test
