#include <catch2/catch_test_macros.hpp>

#include <string>

#include "gfx/canvas.h"
#include "gfx/palette.h"

using phos::gfx::Canvas;
using phos::gfx::Rect;

namespace {
int count(const Canvas& c, std::uint8_t ink) {
    int n = 0;
    for (auto p : c.pixels()) {
        n += p == ink ? 1 : 0;
    }
    return n;
}
} // namespace

TEST_CASE("line covers both endpoints and is continuous", "[canvas]") {
    Canvas c(32, 32);
    c.line(1, 2, 20, 9, 4);
    CHECK(c.get(1, 2) == 4);
    CHECK(c.get(20, 9) == 4);
    CHECK(count(c, 4) == 20); // x-major: one pixel per column
}

TEST_CASE("drawing is clipped to the canvas and the clip rect", "[canvas]") {
    Canvas c(16, 16);
    c.fill_rect(Rect{-5, -5, 40, 40}, 3);
    CHECK(count(c, 3) == 256);
    c.clear(0);
    c.set_clip(Rect{4, 4, 4, 4});
    c.fill_rect(Rect{0, 0, 16, 16}, 2);
    CHECK(count(c, 2) == 16);
    CHECK(c.get(3, 4) == 0);
    CHECK(c.get(4, 4) == 2);
}

TEST_CASE("rect outline has the right perimeter", "[canvas]") {
    Canvas c(20, 20);
    c.rect(Rect{2, 3, 10, 5}, 1);
    CHECK(count(c, 1) == 2 * 10 + 2 * 3);
}

TEST_CASE("circle is eight-way symmetric", "[canvas]") {
    Canvas c(101, 101);
    c.circle(50, 50, 40, 5);
    for (int y = 0; y < 101; ++y) {
        for (int x = 0; x < 101; ++x) {
            const auto v = c.get(x, y);
            REQUIRE(v == c.get(100 - x, y));
            REQUIRE(v == c.get(x, 100 - y));
            REQUIRE(v == c.get(y, x));
        }
    }
    CHECK(c.get(90, 50) == 5);
    CHECK(c.get(50, 10) == 5);
}

TEST_CASE("dotted line plots every n-th pixel", "[canvas]") {
    Canvas c(40, 4);
    c.dotted_line(0, 1, 29, 1, 3, 7);
    CHECK(count(c, 7) == 10);
    CHECK(c.get(0, 1) == 7);
    CHECK(c.get(1, 1) == 0);
    CHECK(c.get(3, 1) == 7);
}

TEST_CASE("stipple fills half the pixels", "[canvas]") {
    Canvas c(8, 8);
    c.stipple_rect(Rect{0, 0, 8, 8}, 9);
    CHECK(count(c, 9) == 32);
}

TEST_CASE("Role inks: single-hue themes reuse their base inks; COLOR gives each its own hue", "[gfx][palette]") {
    using namespace phos::gfx;
    for (const Theme& t : all_themes()) {
        INFO(std::string(t.name));
        CHECK(find_theme(t.name) == &t);
        if (t.name == "COLOR") {
            continue;
        }
        CHECK(t.colors[kInkTab] == t.colors[kInkBright]);
        CHECK(t.colors[kInkSelect] == t.colors[kInkBright]);
        CHECK(t.colors[kInkCursor] == t.colors[kInkBright]);
        CHECK(t.colors[kInkPlay] == t.colors[kInkBright]);
        CHECK(t.colors[kInkLevelLow] == t.colors[kInkText]);
        CHECK(t.colors[kInkLevelMid] == t.colors[kInkText]);
        CHECK(t.colors[kInkLevelHigh] == t.colors[kInkText]);
        CHECK(t.colors[kInkLabel] == t.colors[kInkText]);
    }
    const Theme* c = find_theme("color");
    REQUIRE(c);
    for (int a = kInkTab; a < kPaletteSize; ++a) {
        for (int b = a + 1; b < kPaletteSize; ++b) {
            CHECK_FALSE(c->colors[static_cast<std::size_t>(a)] == c->colors[static_cast<std::size_t>(b)]);
        }
    }
    const Rgb text = c->colors[kInkText];
    CHECK((text.r > 0xE0 && text.g > 0xE0 && text.b > 0xE0)); // white text and lines
}
