#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace phos::gfx {

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    friend bool operator==(const Rect&, const Rect&) = default;
};

// 8-bit indexed framebuffer. Every value is a palette slot (see gfx/palette.h).
// All drawing is clipped to the current clip rect (initially the whole canvas).
class Canvas {
public:
    Canvas(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    std::span<const std::uint8_t> pixels() const { return pixels_; }
    std::span<std::uint8_t> pixels() { return pixels_; }

    void set_clip(Rect r);
    void reset_clip();
    Rect clip() const { return clip_; }

    void clear(std::uint8_t ink);
    void set(int x, int y, std::uint8_t ink);
    std::uint8_t get(int x, int y) const;

    void hline(int x0, int x1, int y, std::uint8_t ink);
    void vline(int x, int y0, int y1, std::uint8_t ink);
    void line(int x0, int y0, int x1, int y1, std::uint8_t ink);
    // Bresenham line that only plots pixels whose step index i satisfies (pattern >> (i % length)) & 1.
    void pattern_line(int x0, int y0, int x1, int y1, std::uint32_t pattern, int length, std::uint8_t ink);
    void dotted_line(int x0, int y0, int x1, int y1, int period, std::uint8_t ink);
    void polyline(std::span<const Point> points, std::uint8_t ink);

    void rect(Rect r, std::uint8_t ink);
    void fill_rect(Rect r, std::uint8_t ink);
    // Checkerboard fill: pixels with (x + y) % 2 == phase get ink.
    void stipple_rect(Rect r, std::uint8_t ink, int phase = 0);
    // Replaces every pixel of ink `from` with `to` inside r (inverse-video helpers).
    void replace_ink(Rect r, std::uint8_t from, std::uint8_t to);

    void circle(int cx, int cy, int radius, std::uint8_t ink);
    void fill_circle(int cx, int cy, int radius, std::uint8_t ink);

    // 1-bit mask blit: `rows` holds h bytes, bit 7 = leftmost pixel. Only set bits are drawn.
    void blit_rows(int x, int y, std::span<const std::uint8_t> rows, int w, std::uint8_t ink);

    // Copies `src` into this canvas at (x, y), clipped. Sizes may differ.
    void copy_from(const Canvas& src, int x = 0, int y = 0);

private:
    bool in_clip(int x, int y) const { return clip_.contains(x, y); }

    int width_;
    int height_;
    Rect clip_;
    std::vector<std::uint8_t> pixels_;
};

} // namespace phos::gfx
