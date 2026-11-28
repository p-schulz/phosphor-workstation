#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include "gfx/canvas.h"
#include "gfx/image_io.h"
#include "gfx/palette.h"
#include "ui/display_profile.h"

namespace phos::platform {

// Canvas → screen: R8UI index texture → palette + phosphor persistence (canvas resolution,
// ping-pong) → half-resolution bloom → CRT pass into the letterboxed viewport.
class CrtRenderer {
public:
    CrtRenderer() = default;
    ~CrtRenderer();
    CrtRenderer(const CrtRenderer&) = delete;
    CrtRenderer& operator=(const CrtRenderer&) = delete;

    bool init(const std::filesystem::path& shader_dir, int canvas_w, int canvas_h, std::string* error);
    // render() takes any canvas size: a different width (side strips) reallocates the targets.

    // `viewport` is top-left origin in framebuffer pixels; `dt_s` drives the persistence decay.
    void render(const gfx::Canvas& canvas, const gfx::Theme& theme, const ui::CrtParams& crt,
                const gfx::Rect& viewport, int fb_w, int fb_h, double dt_s);

    // Reads back the default framebuffer (call after render(), before swap). Top row first.
    std::optional<gfx::Image> read_framebuffer(int fb_w, int fb_h) const;

private:
    struct Target {
        unsigned int texture = 0;
        unsigned int fbo = 0;
    };
    bool allocate(int w, int h, std::string* error);
    bool make_target(Target& t, int w, int h, std::string* error);
    void destroy_target(Target& t);

    int canvas_w_ = 0;
    int canvas_h_ = 0;
    unsigned int vao_ = 0;
    unsigned int canvas_tex_ = 0;
    unsigned int palette_tex_ = 0;
    unsigned int phosphor_prog_ = 0;
    unsigned int blur_prog_ = 0;
    unsigned int crt_prog_ = 0;
    std::array<Target, 2> persist_{};
    std::array<Target, 2> bloom_{};
    int current_ = 0;
};

} // namespace phos::platform
