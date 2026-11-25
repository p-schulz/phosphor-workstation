#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/canvas.h"

namespace phos::gfx {

// Symbol code points shared by both PHOSPHOR fonts (see assets/fonts/src/).
namespace glyph {
inline constexpr char kArrowLeft = '\x80';
inline constexpr char kArrowRight = '\x81';
inline constexpr char kArrowUp = '\x82';
inline constexpr char kArrowDown = '\x83';
inline constexpr char kTriUp = '\x84';
inline constexpr char kTriDown = '\x85';
inline constexpr char kDot = '\x86';
inline constexpr char kSquare = '\x87';
inline constexpr char kRing = '\x88';
inline constexpr char kPlay = '\x89';
inline constexpr char kNote = '\x8A';
inline constexpr char kTriLeft = '\x8B';
inline constexpr char kTriRight = '\x8C';
inline constexpr char kTick = '\x8D';
inline constexpr char kDegree = '\x8E';
inline constexpr char kBlock = '\x8F';
} // namespace glyph

// Monospace 1-bit font, up to 8 pixels wide, 256 code points.
struct BitmapFont {
    std::string name;
    int cell_w = 8;
    int cell_h = 12;
    int baseline = 9;
    std::array<bool, 256> present{};
    std::vector<std::uint8_t> rows; // 256 * cell_h bytes, bit 7 = leftmost pixel

    bool has(unsigned char c) const { return present[c]; }
    std::span<const std::uint8_t> glyph_rows(unsigned char c) const;
    friend bool operator==(const BitmapFont&, const BitmapFont&) = default;
};

// Parses the text glyph source format used in assets/fonts/src/*.txt.
std::optional<BitmapFont> parse_font_source(std::string_view text, std::string* error);

// Atlas layout: 16 x 16 cells of cell_w x cell_h, code point c at column c % 16, row c / 16.
// `gray` is one byte per pixel; values >= 128 are "on".
std::vector<std::uint8_t> font_to_atlas(const BitmapFont& font, int* atlas_w, int* atlas_h);
std::string font_metrics_json(const BitmapFont& font);
std::optional<BitmapFont> font_from_atlas(std::span<const std::uint8_t> gray, int atlas_w, int atlas_h,
                                          std::string_view metrics_json, std::string* error);

// Loads <base>.png + <base>.json.
std::optional<BitmapFont> load_font(const std::filesystem::path& base, std::string* error);

// Draws text at pixel (x, y) (top-left of the first cell). `advance` 0 = cell width.
// Returns the x after the last glyph. Unknown code points render as '?'.
int draw_text(Canvas& canvas, const BitmapFont& font, int x, int y, std::string_view text, std::uint8_t ink,
              int advance = 0);

// Same, but fills each glyph cell with `bg` first.
int draw_text_bg(Canvas& canvas, const BitmapFont& font, int x, int y, std::string_view text, std::uint8_t ink,
                 std::uint8_t bg, int advance = 0);

} // namespace phos::gfx
