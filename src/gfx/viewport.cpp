#include "gfx/viewport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace phos::gfx {

std::string_view scale_mode_name(ScaleMode mode) {
    switch (mode) {
    case ScaleMode::Fit:
        return "FIT";
    case ScaleMode::Integer:
        return "INTEGER";
    case ScaleMode::Stretch:
        return "STRETCH";
    }
    return "FIT";
}

std::optional<ScaleMode> parse_scale_mode(std::string_view name) {
    std::string upper(name);
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upper == "FIT") {
        return ScaleMode::Fit;
    }
    if (upper == "INTEGER" || upper == "INT") {
        return ScaleMode::Integer;
    }
    if (upper == "STRETCH") {
        return ScaleMode::Stretch;
    }
    return std::nullopt;
}

namespace {

Rect fit(const Rect& avail, double aspect) {
    Rect r;
    if (static_cast<double>(avail.w) / static_cast<double>(avail.h) > aspect) {
        r.h = avail.h;
        r.w = static_cast<int>(std::lround(avail.h * aspect));
    } else {
        r.w = avail.w;
        r.h = static_cast<int>(std::lround(avail.w / aspect));
    }
    r.w = std::max(r.w, 1);
    r.h = std::max(r.h, 1);
    r.x = avail.x + (avail.w - r.w) / 2;
    r.y = avail.y + (avail.h - r.h) / 2;
    return r;
}

} // namespace

Rect compute_viewport(const ViewportParams& p) {
    if (p.fb_w <= 0 || p.fb_h <= 0 || p.canvas_w <= 0 || p.canvas_h <= 0) {
        return Rect{};
    }
    const auto inset = [](int size, float frac) {
        return static_cast<int>(std::lround(size * std::clamp(static_cast<double>(frac), 0.0, 0.45)));
    };
    Rect avail;
    avail.x = inset(p.fb_w, p.overscan.left);
    avail.y = inset(p.fb_h, p.overscan.top);
    avail.w = std::max(1, p.fb_w - avail.x - inset(p.fb_w, p.overscan.right));
    avail.h = std::max(1, p.fb_h - avail.y - inset(p.fb_h, p.overscan.bottom));

    const double par = p.pixel_aspect > 0.0 ? p.pixel_aspect : 1.0;
    // Aspect the viewport needs in framebuffer pixels so the canvas looks canvas_w:canvas_h physically.
    const double aspect = (static_cast<double>(p.canvas_w) / p.canvas_h) / par;

    switch (p.mode) {
    case ScaleMode::Stretch:
        return avail;
    case ScaleMode::Integer: {
        for (int k = avail.h / p.canvas_h; k >= 1; --k) {
            const int w = static_cast<int>(std::lround(p.canvas_w * k / par));
            if (w <= avail.w) {
                const int h = p.canvas_h * k;
                return Rect{avail.x + (avail.w - w) / 2, avail.y + (avail.h - h) / 2, w, h};
            }
        }
        return fit(avail, aspect); // window smaller than 1x: fall back to FIT
    }
    case ScaleMode::Fit:
        break;
    }
    return fit(avail, aspect);
}

double pixel_aspect_from_panel(double panel_aspect, int signal_w, int signal_h) {
    if (panel_aspect <= 0.0 || signal_w <= 0 || signal_h <= 0) {
        return 1.0;
    }
    return panel_aspect * static_cast<double>(signal_h) / static_cast<double>(signal_w);
}

void crt_distort(float curvature, float& u, float& v) {
    if (curvature <= 0.0f) {
        return;
    }
    const float k = curvature * 0.25f;
    const float cx = u - 0.5f;
    const float cy = v - 0.5f;
    const float r2 = cx * cx + cy * cy;
    const float s = 1.0f + k * r2;
    u = 0.5f + cx * s;
    v = 0.5f + cy * s;
}

std::optional<CanvasPos> framebuffer_to_canvas(const Rect& viewport, int canvas_w, int canvas_h, float curvature,
                                               double fb_x, double fb_y) {
    if (viewport.w <= 0 || viewport.h <= 0) {
        return std::nullopt;
    }
    float u = static_cast<float>((fb_x - viewport.x) / viewport.w);
    float v = static_cast<float>((fb_y - viewport.y) / viewport.h);
    crt_distort(curvature, u, v);
    if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f) {
        return std::nullopt;
    }
    return CanvasPos{u * static_cast<float>(canvas_w), v * static_cast<float>(canvas_h)};
}

} // namespace phos::gfx
