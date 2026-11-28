#include "gfx/palette.h"

#include <cctype>

namespace phos::gfx {

namespace {

constexpr Rgb rgb(std::uint32_t hex) {
    return Rgb{static_cast<std::uint8_t>((hex >> 16) & 0xFF), static_cast<std::uint8_t>((hex >> 8) & 0xFF),
               static_cast<std::uint8_t>(hex & 0xFF)};
}

using Base = std::array<Rgb, 16>;

// A single-hue theme: the role colours repeat the base inks (see palette.h), so every page looks
// exactly as it did before the role colours existed.
constexpr Theme mono(std::string_view name, const Base& b) {
    Theme t{name, {}};
    for (std::size_t i = 0; i < b.size(); ++i) {
        t.colors[i] = b[i];
    }
    t.colors[kInkTab] = b[kInkBright];
    t.colors[kInkSelect] = b[kInkBright];
    t.colors[kInkCursor] = b[kInkBright];
    t.colors[kInkLevelLow] = b[kInkText];
    t.colors[kInkLevelMid] = b[kInkText];
    t.colors[kInkLevelHigh] = b[kInkText];
    t.colors[kInkPlay] = b[kInkBright];
    t.colors[kInkLabel] = b[kInkText];
    return t;
}

constexpr Theme unlit(Theme t) {
    t.glow = false;
    return t;
}

// Each base: bg, field, grid, dim, text, bright, accent, warn, pen, shade, ramp1..ramp6.
constexpr std::array<Theme, 7> kThemes = {{
    // P1 green phosphor.
    mono("GREEN",
         {rgb(0x000000), rgb(0x053A1A), rgb(0x0E6B33), rgb(0x138544), rgb(0x2BE07A), rgb(0x7DFFB0), rgb(0x3FF0E0),
          rgb(0xE8F060), rgb(0xB8FFD4), rgb(0x0B4F26), rgb(0x0A4021), rgb(0x0F5E2F), rgb(0x168040), rgb(0x1EA553),
          rgb(0x28C867), rgb(0x52F293)}),
    // P3 amber.
    mono("AMBER",
         {rgb(0x000000), rgb(0x3A2104), rgb(0x6B3E08), rgb(0x9C5C0E), rgb(0xFFA62B), rgb(0xFFD27A), rgb(0xFFE7B0),
          rgb(0xFF5A3C), rgb(0xFFE2B0), rgb(0x4F2E06), rgb(0x402505), rgb(0x5E3707), rgb(0x80490A), rgb(0xA55F0D),
          rgb(0xC87411), rgb(0xF29A20)}),
    // P4 white on black.
    mono("GREY",
         {rgb(0x000000), rgb(0x1E2328), rgb(0x3C434A), rgb(0x6E7881), rgb(0xC8D2DA), rgb(0xF2F6FA), rgb(0x9FC4E8),
          rgb(0xF0C040), rgb(0xFFFFFF), rgb(0x2B3138), rgb(0x262B31), rgb(0x3A4148), rgb(0x525A62), rgb(0x6E7780),
          rgb(0x9098A0), rgb(0xBCC4CC)}),
    // Vaporwave: magenta/cyan on violet.
    mono("VAPOR",
         {rgb(0x1A0B2E), rgb(0x34145A), rgb(0x4C2380), rgb(0x8A5CC0), rgb(0xFF71CE), rgb(0xFFC6EE), rgb(0x01CDFE),
          rgb(0xFFFB96), rgb(0x05FFA1), rgb(0x3A1966), rgb(0x2E1452), rgb(0x47206F), rgb(0x6A2C8E), rgb(0x9A3AAE),
          rgb(0xD04CC2), rgb(0xFF8CDA)}),
    // Cyan monitor (as in two of the reference screens).
    mono("CYAN",
         {rgb(0x000000), rgb(0x05464E), rgb(0x0B6E78), rgb(0x1499A6), rgb(0x20E6F2), rgb(0x9AF8FF), rgb(0xFFFFFF),
          rgb(0xF0E060), rgb(0xC8FCFF), rgb(0x0A5560), rgb(0x063E45), rgb(0x0A5A63), rgb(0x0F7A85), rgb(0x149CA8),
          rgb(0x1AC0CC), rgb(0x52EEF8)}),
    // COLOR: white text and lines on near-black; role colours from a colour graphics terminal.
    // Tabs cyan, selection amber, cursor and playheads magenta, meters green / yellow / red,
    // running things green, labels pale blue; depth ramp violet -> magenta -> cyan (3D plots).
    {"COLOR",
     {rgb(0x0A0C12), rgb(0x1B2130), rgb(0x343C4E), rgb(0x8A93A8), rgb(0xF2F4F8), rgb(0xFFFFFF), rgb(0x3DDCFF),
      rgb(0xFF4D5E), rgb(0xFF5CD6), rgb(0x232A3A), rgb(0x3A1F66), rgb(0x6B2A9E), rgb(0xA83AC8), rgb(0xE04FD8),
      rgb(0x7F8CFF), rgb(0x3DF2FF),
      // tab, select, cursor, level low, level mid, level high, play, label
      rgb(0x2EC4F0), rgb(0xFFC23D), rgb(0xFF4FD8), rgb(0x3DDC6E), rgb(0xFFD93D), rgb(0xFF4D4D), rgb(0x46E27A),
      rgb(0xA9C8FF)}},
    // LCD: a 1990s monochrome LCD, as in early handheld consoles and pocket organisers. Inverted:
    // dark olive "pixels" on a yellow-green panel; "bright" means darkest, so inverse video is a dark
    // band with panel-coloured text. The depth ramp runs from faint (back) to near-black (front).
    unlit(mono("LCD",
               {rgb(0xB4BE3E), rgb(0xA3AE36), rgb(0x8E9A34), rgb(0x66742A), rgb(0x2C3E16), rgb(0x16240C),
                rgb(0x3F5A1A), rgb(0x4E3A0C), rgb(0x0E1A08), rgb(0xA7B239), rgb(0x99A535), rgb(0x84922F),
                rgb(0x6B7A28), rgb(0x4F6020), rgb(0x354818), rgb(0x1C2E0E)})),
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
