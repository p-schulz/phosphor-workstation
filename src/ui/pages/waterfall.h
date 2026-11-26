#pragma once

#include <cstdint>
#include <functional>

#include "gfx/canvas.h"

// Page D's waterfall with hidden-line removal (floating horizon): rows are drawn front to back and
// a row only shows where it rises above everything drawn in front of it, so a dense stack reads as
// one surface instead of overlapping wires.
namespace phos::ui::wave {

struct WaterfallView {
    double dx = 1.2;    // canvas px per row, to the right
    double dy = 1.1;    // canvas px per row, upwards
    double amp = 50.0;  // canvas px for a value of 1
    bool hidden = true; // hidden-line removal (off: wireframe, all rows complete)
};

// `rows` polylines of `points` values (-1..1); row 0 is the front (bottom left), row rows-1 the back.
// `ink(row)` picks each row's colour.
void waterfall(gfx::Canvas& c, gfx::Rect area, int rows, int points, const WaterfallView& view,
               const std::function<float(int, int)>& value, const std::function<std::uint8_t(int)>& ink);

} // namespace phos::ui::wave
