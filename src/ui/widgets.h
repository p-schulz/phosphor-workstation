#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "gfx/canvas.h"
#include "gfx/palette.h"
#include "ui/ui.h"

// Drawing helpers for the screen vocabulary seen in the reference pages: labels, inverse title
// tabs, value fields on a dark band, option groups with an inverse selection, framed panels.
namespace phos::ui::widgets {

// Text in the 8x12 font at a cell position.
void text(gfx::Canvas& c, const Ui& ui, int col, int row, std::string_view s, std::uint8_t ink = gfx::kInkText);
// Text in the 8x12 font at a pixel position.
void text_px(gfx::Canvas& c, const Ui& ui, int x, int y, std::string_view s, std::uint8_t ink = gfx::kInkText);
// Text in the 8x8 font at a pixel position; `advance` defaults to 6 px (compact labels).
void small_text(gfx::Canvas& c, const Ui& ui, int x, int y, std::string_view s, std::uint8_t ink = gfx::kInkText,
                int advance = 6);
// Centered text within [col, col + width) on `row`.
void centered_text(gfx::Canvas& c, const Ui& ui, int col, int width, int row, std::string_view s,
                   std::uint8_t ink = gfx::kInkText);

// Inverse-video label (bright fill, background-coloured text) with 2 px side padding.
void title_tab(gfx::Canvas& c, const Ui& ui, int x, int y, std::string_view s);
// Frame with an optional title tab centered on its top edge.
void frame(gfx::Canvas& c, const Ui& ui, gfx::Rect r, std::string_view title = {}, std::uint8_t ink = gfx::kInkText);

// "LABEL      =" at (col,row), '=' at col + label_cols.
void label(gfx::Canvas& c, const Ui& ui, int col, int row, std::string_view s, int label_cols);

// Value field: dark band of `width` cells; focused = inverse. Registers a page zone.
void value_field(gfx::Canvas& c, const Ui& ui, int col, int row, int width, std::string_view value, bool focused,
                 int zone);

// Option group: options separated by one space, the selected one inverse. Registers zone
// `zone_base + i` for each option. Returns the column after the last option.
int option_group(gfx::Canvas& c, const Ui& ui, int col, int row, std::span<const std::string_view> options,
                 int selected, bool focused, int zone_base);

// Push button: framed caption; registers a page zone.
void button(gfx::Canvas& c, const Ui& ui, int col, int row, std::string_view caption, bool focused, int zone);

} // namespace phos::ui::widgets
