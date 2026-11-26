#include "ui/pages/waterfall.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <vector>

#include "ui/layout.h"

namespace phos::ui::wave {

void waterfall(gfx::Canvas& c, gfx::Rect area, int rows, int points, const WaterfallView& view,
               const std::function<float(int, int)>& value, const std::function<std::uint8_t(int)>& ink) {
    if (rows <= 0 || points <= 1) {
        return;
    }
    const double width = area.w - view.dx * (rows - 1);
    const double base_y = area.y + area.h - view.amp - 2;
    // horizon[x]: the highest pixel (smallest y) drawn so far in column x; top[x] the current row's.
    std::array<int, kCanvasW> horizon{};
    std::array<int, kCanvasW> top{};
    horizon.fill(INT_MAX);
    std::vector<gfx::Point> line(static_cast<std::size_t>(points));
    for (int r = 0; r < rows; ++r) { // front to back
        const double ox = area.x + view.dx * r;
        const double oy = base_y - view.dy * r;
        for (int i = 0; i < points; ++i) {
            line[static_cast<std::size_t>(i)] = gfx::Point{static_cast<int>(ox + width * i / (points - 1)),
                                                           static_cast<int>(oy - value(r, i) * view.amp)};
        }
        const std::uint8_t row_ink = ink(r);
        if (!view.hidden) {
            c.polyline(line, row_ink);
            continue;
        }
        top.fill(INT_MAX);
        auto plot = [&](int x, int y) {
            if (x < 0 || x >= kCanvasW) {
                return;
            }
            const auto ix = static_cast<std::size_t>(x);
            if (y < horizon[ix]) {
                c.set(x, y, row_ink);
            }
            top[ix] = std::min(top[ix], y);
        };
        for (std::size_t i = 1; i < line.size(); ++i) { // Bresenham, as Canvas::line
            int x0 = line[i - 1].x;
            int y0 = line[i - 1].y;
            const int x1 = line[i].x;
            const int y1 = line[i].y;
            const int sx = x0 < x1 ? 1 : -1;
            const int sy = y0 < y1 ? 1 : -1;
            const int ddx = std::abs(x1 - x0);
            const int ddy = -std::abs(y1 - y0);
            int err = ddx + ddy;
            while (true) {
                plot(x0, y0);
                if (x0 == x1 && y0 == y1) {
                    break;
                }
                const int e2 = 2 * err;
                if (e2 >= ddy) {
                    err += ddy;
                    x0 += sx;
                }
                if (e2 <= ddx) {
                    err += ddx;
                    y0 += sy;
                }
            }
        }
        for (std::size_t x = 0; x < horizon.size(); ++x) {
            horizon[x] = std::min(horizon[x], top[x]);
        }
    }
}

} // namespace phos::ui::wave
