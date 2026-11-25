#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gfx/viewport.h"

using namespace phos::gfx;

TEST_CASE("FIT pillarboxes a 4:3 canvas on 16:9", "[viewport]") {
    ViewportParams p;
    p.fb_w = 1920;
    p.fb_h = 1080;
    CHECK(compute_viewport(p) == Rect{240, 0, 1440, 1080});
}

TEST_CASE("FIT fills a 4:3 framebuffer exactly", "[viewport]") {
    ViewportParams p;
    p.fb_w = 1024;
    p.fb_h = 768;
    CHECK(compute_viewport(p) == Rect{0, 0, 1024, 768});
}

TEST_CASE("INTEGER picks the largest whole multiple", "[viewport]") {
    ViewportParams p;
    p.fb_w = 1920;
    p.fb_h = 1080;
    p.mode = ScaleMode::Integer;
    CHECK(compute_viewport(p) == Rect{448, 156, 1024, 768});
    p.fb_w = 400; // smaller than 1x falls back to FIT
    p.fb_h = 300;
    CHECK(compute_viewport(p) == Rect{0, 0, 400, 300});
}

TEST_CASE("STRETCH fills the available area", "[viewport]") {
    ViewportParams p;
    p.fb_w = 1920;
    p.fb_h = 1080;
    p.mode = ScaleMode::Stretch;
    CHECK(compute_viewport(p) == Rect{0, 0, 1920, 1080});
}

TEST_CASE("panel aspect override: 4:3 panel fed 1920x1080 needs the full signal width", "[viewport]") {
    const double par = pixel_aspect_from_panel(4.0 / 3.0, 1920, 1080);
    CHECK(par == Catch::Approx(0.75));
    ViewportParams p;
    p.fb_w = 1920;
    p.fb_h = 1080;
    p.pixel_aspect = par;
    // 4:3 canvas / 0.75 = 16:9 in signal pixels: fills the signal, appears 4:3 on the panel.
    CHECK(compute_viewport(p) == Rect{0, 0, 1920, 1080});
    CHECK(pixel_aspect_from_panel(0.0, 1920, 1080) == 1.0);
}

TEST_CASE("overscan insets shrink the area before fitting", "[viewport]") {
    ViewportParams p;
    p.fb_w = 1000;
    p.fb_h = 750;
    p.overscan = Insets{0.05f, 0.05f, 0.0f, 0.0f};
    const Rect r = compute_viewport(p);
    CHECK(r.w == 900);
    CHECK(r.h == 675);
    CHECK(r.x == 50);
    CHECK(r.y == 37);
}

TEST_CASE("framebuffer_to_canvas inverts the letterbox", "[viewport]") {
    const Rect vp{240, 0, 1440, 1080};
    const auto mid = framebuffer_to_canvas(vp, 512, 384, 0.0f, 240 + 720, 540);
    REQUIRE(mid);
    CHECK(mid->x == Catch::Approx(256.0f));
    CHECK(mid->y == Catch::Approx(192.0f));
    CHECK_FALSE(framebuffer_to_canvas(vp, 512, 384, 0.0f, 100, 540)); // in the pillarbox
    const auto corner = framebuffer_to_canvas(vp, 512, 384, 0.0f, 240, 0);
    REQUIRE(corner);
    CHECK(corner->x == Catch::Approx(0.0f));
}

TEST_CASE("curvature keeps the centre and pushes edges outward", "[viewport]") {
    float u = 0.5f;
    float v = 0.5f;
    crt_distort(0.5f, u, v);
    CHECK(u == Catch::Approx(0.5f));
    u = 0.95f;
    v = 0.5f;
    crt_distort(0.5f, u, v);
    CHECK(u > 0.95f);
}
