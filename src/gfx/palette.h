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
};

inline constexpr int kPaletteSize = 16;

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

struct Theme {
    std::string_view name;
    std::array<Rgb, kPaletteSize> colors;
};

std::span<const Theme> all_themes();
// Case-insensitive lookup. Returns nullptr when unknown.
const Theme* find_theme(std::string_view name);
const Theme& default_theme();

} // namespace phos::gfx
