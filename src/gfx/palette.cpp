#include "gfx/palette.h"

#include <cctype>

namespace phos::gfx {

namespace {

constexpr Rgb rgb(std::uint32_t hex) {
    return Rgb{static_cast<std::uint8_t>((hex >> 16) & 0xFF), static_cast<std::uint8_t>((hex >> 8) & 0xFF),
               static_cast<std::uint8_t>(hex & 0xFF)};
}

// Each theme: bg, field, grid, dim, text, bright, accent, warn, pen, shade, ramp1..ramp6.
constexpr std::array<Theme, 5> kThemes = {{
    // P1 green phosphor.
    {"GREEN",
     {rgb(0x000000), rgb(0x053A1A), rgb(0x0E6B33), rgb(0x138544), rgb(0x2BE07A), rgb(0x7DFFB0), rgb(0x3FF0E0),
      rgb(0xE8F060), rgb(0xB8FFD4), rgb(0x0B4F26), rgb(0x0A4021), rgb(0x0F5E2F), rgb(0x168040), rgb(0x1EA553),
      rgb(0x28C867), rgb(0x52F293)}},
    // P3 amber.
    {"AMBER",
     {rgb(0x000000), rgb(0x3A2104), rgb(0x6B3E08), rgb(0x9C5C0E), rgb(0xFFA62B), rgb(0xFFD27A), rgb(0xFFE7B0),
      rgb(0xFF5A3C), rgb(0xFFE2B0), rgb(0x4F2E06), rgb(0x402505), rgb(0x5E3707), rgb(0x80490A), rgb(0xA55F0D),
      rgb(0xC87411), rgb(0xF29A20)}},
    // P4 white on black.
    {"GREY",
     {rgb(0x000000), rgb(0x1E2328), rgb(0x3C434A), rgb(0x6E7881), rgb(0xC8D2DA), rgb(0xF2F6FA), rgb(0x9FC4E8),
      rgb(0xF0C040), rgb(0xFFFFFF), rgb(0x2B3138), rgb(0x262B31), rgb(0x3A4148), rgb(0x525A62), rgb(0x6E7780),
      rgb(0x9098A0), rgb(0xBCC4CC)}},
    // Vaporwave: magenta/cyan on violet.
    {"VAPOR",
     {rgb(0x1A0B2E), rgb(0x34145A), rgb(0x4C2380), rgb(0x8A5CC0), rgb(0xFF71CE), rgb(0xFFC6EE), rgb(0x01CDFE),
      rgb(0xFFFB96), rgb(0x05FFA1), rgb(0x3A1966), rgb(0x2E1452), rgb(0x47206F), rgb(0x6A2C8E), rgb(0x9A3AAE),
      rgb(0xD04CC2), rgb(0xFF8CDA)}},
    // Cyan monitor (as in two of the reference screens).
    {"CYAN",
     {rgb(0x000000), rgb(0x05464E), rgb(0x0B6E78), rgb(0x1499A6), rgb(0x20E6F2), rgb(0x9AF8FF), rgb(0xFFFFFF),
      rgb(0xF0E060), rgb(0xC8FCFF), rgb(0x0A5560), rgb(0x063E45), rgb(0x0A5A63), rgb(0x0F7A85), rgb(0x149CA8),
      rgb(0x1AC0CC), rgb(0x52EEF8)}},
}};

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::span<const Theme> all_themes() { return kThemes; }

const Theme* find_theme(std::string_view name) {
    for (const Theme& t : kThemes) {
        if (iequals(t.name, name)) {
            return &t;
        }
    }
    return nullptr;
}

const Theme& default_theme() { return kThemes[0]; }

} // namespace phos::gfx
