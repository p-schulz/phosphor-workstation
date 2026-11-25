#include "gfx/canvas.h"

#include <algorithm>
#include <cstdlib>

namespace phos::gfx {

Canvas::Canvas(int width, int height)
    : width_(std::max(width, 1)), height_(std::max(height, 1)), clip_{0, 0, width_, height_},
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0) {}

void Canvas::set_clip(Rect r) {
    const int x0 = std::clamp(r.x, 0, width_);
    const int y0 = std::clamp(r.y, 0, height_);
    const int x1 = std::clamp(r.x + r.w, 0, width_);
    const int y1 = std::clamp(r.y + r.h, 0, height_);
    clip_ = Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Canvas::reset_clip() { clip_ = Rect{0, 0, width_, height_}; }

void Canvas::clear(std::uint8_t ink) { std::fill(pixels_.begin(), pixels_.end(), ink); }

void Canvas::set(int x, int y, std::uint8_t ink) {
    if (in_clip(x, y)) {
        pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = ink;
    }
}

std::uint8_t Canvas::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return 0;
    }
    return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

void Canvas::hline(int x0, int x1, int y, std::uint8_t ink) {
    if (x0 > x1) {
        std::swap(x0, x1);
    }
    if (y < clip_.y || y >= clip_.y + clip_.h) {
        return;
    }
    x0 = std::max(x0, clip_.x);
    x1 = std::min(x1, clip_.x + clip_.w - 1);
    for (int x = x0; x <= x1; ++x) {
        pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = ink;
    }
}

void Canvas::vline(int x, int y0, int y1, std::uint8_t ink) {
    if (y0 > y1) {
        std::swap(y0, y1);
    }
    for (int y = y0; y <= y1; ++y) {
        set(x, y, ink);
    }
}

void Canvas::line(int x0, int y0, int x1, int y1, std::uint8_t ink) { pattern_line(x0, y0, x1, y1, 1u, 1, ink); }

void Canvas::pattern_line(int x0, int y0, int x1, int y1, std::uint32_t pattern, int length, std::uint8_t ink) {
    length = std::clamp(length, 1, 32);
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int step = 0;
    for (;;) {
        if ((pattern >> (step % length)) & 1u) {
            set(x0, y0, ink);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
        ++step;
    }
}

void Canvas::dotted_line(int x0, int y0, int x1, int y1, int period, std::uint8_t ink) {
    period = std::clamp(period, 1, 32);
    pattern_line(x0, y0, x1, y1, 1u, period, ink);
}

void Canvas::polyline(std::span<const Point> points, std::uint8_t ink) {
    for (std::size_t i = 1; i < points.size(); ++i) {
        line(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, ink);
    }
    if (points.size() == 1) {
        set(points[0].x, points[0].y, ink);
    }
}

void Canvas::rect(Rect r, std::uint8_t ink) {
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    hline(r.x, r.x + r.w - 1, r.y, ink);
    hline(r.x, r.x + r.w - 1, r.y + r.h - 1, ink);
    vline(r.x, r.y, r.y + r.h - 1, ink);
    vline(r.x + r.w - 1, r.y, r.y + r.h - 1, ink);
}

void Canvas::fill_rect(Rect r, std::uint8_t ink) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        hline(r.x, r.x + r.w - 1, y, ink);
    }
}

void Canvas::stipple_rect(Rect r, std::uint8_t ink, int phase) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            if (((x + y) & 1) == (phase & 1)) {
                set(x, y, ink);
            }
        }
    }
}

void Canvas::replace_ink(Rect r, std::uint8_t from, std::uint8_t to) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            if (in_clip(x, y) && get(x, y) == from) {
                set(x, y, to);
            }
        }
    }
}

void Canvas::circle(int cx, int cy, int radius, std::uint8_t ink) {
    // Midpoint circle algorithm, eight-way symmetric.
    int x = radius;
    int y = 0;
    int err = 1 - radius;
    while (x >= y) {
        set(cx + x, cy + y, ink);
        set(cx + y, cy + x, ink);
        set(cx - y, cy + x, ink);
        set(cx - x, cy + y, ink);
        set(cx - x, cy - y, ink);
        set(cx - y, cy - x, ink);
        set(cx + y, cy - x, ink);
        set(cx + x, cy - y, ink);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void Canvas::fill_circle(int cx, int cy, int radius, std::uint8_t ink) {
    const int r2 = radius * radius + radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                set(cx + dx, cy + dy, ink);
            }
        }
    }
}

void Canvas::blit_rows(int x, int y, std::span<const std::uint8_t> rows, int w, std::uint8_t ink) {
    w = std::clamp(w, 0, 8);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const std::uint8_t bits = rows[row];
        if (bits == 0) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            if (bits & (0x80u >> col)) {
                set(x + col, y + static_cast<int>(row), ink);
            }
        }
    }
}

void Canvas::copy_from(const Canvas& src, int x, int y) {
    for (int sy = 0; sy < src.height(); ++sy) {
        for (int sx = 0; sx < src.width(); ++sx) {
            set(x + sx, y + sy, src.get(sx, sy));
        }
    }
}

} // namespace phos::gfx
