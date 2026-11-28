#pragma once

#include <optional>
#include <string_view>

#include "gfx/canvas.h"

namespace phos::gfx {

enum class ScaleMode { Fit, Integer, Stretch };

std::string_view scale_mode_name(ScaleMode mode);
std::optional<ScaleMode> parse_scale_mode(std::string_view name);

// Overscan insets as fractions of the framebuffer size per edge (0.05 = 5 %).
struct Insets {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    friend bool operator==(const Insets&, const Insets&) = default;
};

struct ViewportParams {
    int fb_w = 0;
    int fb_h = 0;
    int canvas_w = 512;
    int canvas_h = 384;
    ScaleMode mode = ScaleMode::Fit;
    // Physical width / height of one framebuffer pixel. 1.0 = square pixels.
    double pixel_aspect = 1.0;
    Insets overscan;
};

// Where the canvas lands in the framebuffer (top-left origin, framebuffer pixels).
Rect compute_viewport(const ViewportParams& p);

// Side keys: touch / click strips left and right of the 4:3 canvas when the screen is wider
// (16:9 panels, a MacBook screen, Sidecar). OFF: never. AUTO: when the margins hold at least
// kSideMinW canvas pixels. ON: always; the canvas shrinks to make room if needed.
enum class SideKeys { Off, Auto, On };
inline constexpr int kSideMinW = 48;  // canvas pixels per strip
inline constexpr int kSideMaxW = 112;
std::string_view side_keys_name(SideKeys mode);
std::optional<SideKeys> parse_side_keys(std::string_view name);

// The whole screen: the canvas plus a side strip of `side_w` canvas pixels on each edge.
// `viewport` covers (canvas_w + 2 * side_w) x canvas_h canvas pixels.
struct ScreenLayout {
    Rect viewport;
    int side_w = 0;
};
ScreenLayout compute_screen_layout(const ViewportParams& p, SideKeys mode);

// Pixel aspect when a panel of physical aspect `panel_aspect` (e.g. 4/3) shows a signal of
// signal_w x signal_h (e.g. a cheap 4:3 controller fed 1920x1080). panel_aspect <= 0 means
// "panel matches the signal" and returns 1.0.
double pixel_aspect_from_panel(double panel_aspect, int signal_w, int signal_h);

// Barrel distortion used by shaders/crt.frag: maps an output uv (0..1) to the source uv it shows.
// Keep in sync with the shader.
void crt_distort(float curvature, float& u, float& v);

// Maps a framebuffer position to canvas pixel coordinates (inverse letterbox + curvature).
// Returns nullopt outside the visible canvas.
struct CanvasPos {
    float x = 0.0f;
    float y = 0.0f;
};
std::optional<CanvasPos> framebuffer_to_canvas(const Rect& viewport, int canvas_w, int canvas_h, float curvature,
                                               double fb_x, double fb_y);

} // namespace phos::gfx
