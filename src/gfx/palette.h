#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace phos::gfx {

// Semantic palette slots. The canvas stores these indices; a Theme maps them to RGB.
enum Ink : std::uint8_t {
    kInkBg = 0,      // background
    kInkField = 1,   // dark fill: value fields, command line band, info band
    kInkGrid = 2,    // dim rules, dotted grids, disabled frames
    kInkDim = 3,     // secondary / unavailable text
    kInkText = 4,    // normal text, frames, traces
    kInkBright = 5,  // inverse-video fill, highlights
    kInkAccent = 6,  // second hue (newest trace, selection accents)
    kInkWarn = 7,    // warnings
    kInkPen = 8,     // light-pen crosshair
    kInkShade = 9,   // stipple mid tone (empty soft keys)
    kInkRamp1 = 10,  // 10..15: depth ramp, dim → bright (waterfalls, meters)
    kInkRamp2 = 11,
    kInkRamp3 = 12,
    kInkRamp4 = 13,
    kInkRamp5 = 14,
    kInkRamp6 = 15,
    // Role colours (16..23). The single-hue themes give them the colour of the ink they stand in
    // for (noted in brackets), so those themes look exactly as before; COLOR gives each its own hue.
    kInkTab = 16,     // title tabs and the header banner [bright]
    kInkSelect = 17,  // selected option, focused field, pressed button [bright]
    kInkCursor = 18,  // text cursor, playheads, focus outlines [bright]
    kInkLevelLow = 19, // meters up to -18 dBFS [text]
    kInkLevelMid = 20, // meters -18..-6 dBFS [text]
    kInkLevelHigh = 21, // meters above -6 dBFS [text]
    kInkPlay = 22,    // running: transport, playing loops [bright]
    kInkLabel = 23,   // field labels and units [text]
};

inline constexpr int kPaletteSize = 24;

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

struct Theme {
    std::string_view name;
    std::array<Rgb, kPaletteSize> colors;
    // Emissive screen (phosphor): the CRT pass adds bloom and the shadow mask. false = a reflective LCD:
    // no glow, no mask, scanlines only as a faint pixel grid.
    bool glow = true;
};

std::span<const Theme> all_themes();
// Case-insensitive lookup. Returns nullptr when unknown.
const Theme* find_theme(std::string_view name);
const Theme& default_theme();

} // namespace phos::gfx
