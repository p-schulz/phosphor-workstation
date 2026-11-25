#include "platform/crt_renderer_gl.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

#include <glad/gl.h>

namespace phos::platform {

namespace {

bool read_text(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

GLuint compile(GLenum type, const std::string& source, const std::string& name, std::string* error) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<std::size_t>(std::max(len, 1)));
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        *error = "shader " + name + ": " + log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint load_program(const std::filesystem::path& dir, const char* vert, const char* frag, std::string* error) {
    std::string vs;
    std::string fs;
    if (!read_text(dir / vert, vs) || !read_text(dir / frag, fs)) {
        *error = "cannot read shaders " + std::string(vert) + " / " + frag + " in " + dir.string();
        return 0;
    }
    const GLuint v = compile(GL_VERTEX_SHADER, vs, vert, error);
    const GLuint f = v ? compile(GL_FRAGMENT_SHADER, fs, frag, error) : 0;
    if (!v || !f) {
        if (v) {
            glDeleteShader(v);
        }
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<std::size_t>(std::max(len, 1)));
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        *error = std::string("link ") + frag + ": " + log.data();
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void set_texture_params(GLint filter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GLint loc(GLuint prog, const char* name) { return glGetUniformLocation(prog, name); }

} // namespace

CrtRenderer::~CrtRenderer() {
    for (auto& t : persist_) {
        destroy_target(t);
    }
    for (auto& t : bloom_) {
        destroy_target(t);
    }
    if (canvas_tex_) {
        glDeleteTextures(1, &canvas_tex_);
    }
    if (palette_tex_) {
        glDeleteTextures(1, &palette_tex_);
    }
    for (GLuint p : {phosphor_prog_, blur_prog_, crt_prog_}) {
        if (p) {
            glDeleteProgram(p);
        }
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
    }
}

bool CrtRenderer::make_target(Target& t, int w, int h, std::string* error) {
    glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    set_texture_params(GL_LINEAR);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        *error = "framebuffer incomplete";
    }
    return ok;
}

void CrtRenderer::destroy_target(Target& t) {
    if (t.fbo) {
        glDeleteFramebuffers(1, &t.fbo);
    }
    if (t.texture) {
        glDeleteTextures(1, &t.texture);
    }
    t = Target{};
}

bool CrtRenderer::init(const std::filesystem::path& shader_dir, int canvas_w, int canvas_h, std::string* error) {
    canvas_w_ = canvas_w;
    canvas_h_ = canvas_h;
    phosphor_prog_ = load_program(shader_dir, "fullscreen.vert", "phosphor.frag", error);
    blur_prog_ = phosphor_prog_ ? load_program(shader_dir, "fullscreen.vert", "blur.frag", error) : 0;
    crt_prog_ = blur_prog_ ? load_program(shader_dir, "fullscreen.vert", "crt.frag", error) : 0;
    if (!crt_prog_) {
        return false;
    }
    glGenVertexArrays(1, &vao_);

    glGenTextures(1, &canvas_tex_);
    glBindTexture(GL_TEXTURE_2D, canvas_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, canvas_w, canvas_h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    set_texture_params(GL_NEAREST);

    glGenTextures(1, &palette_tex_);
    glBindTexture(GL_TEXTURE_2D, palette_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gfx::kPaletteSize, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    set_texture_params(GL_NEAREST);

    for (auto& t : persist_) {
        if (!make_target(t, canvas_w, canvas_h, error)) {
            return false;
        }
    }
    for (auto& t : bloom_) {
        if (!make_target(t, canvas_w / 2, canvas_h / 2, error)) {
            return false;
        }
    }
    return true;
}

void CrtRenderer::render(const gfx::Canvas& canvas, const gfx::Theme& theme, const ui::CrtParams& crt,
                         const gfx::Rect& viewport, int fb_w, int fb_h, double dt_s) {
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, canvas_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, canvas_w_, canvas_h_, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                    canvas.pixels().data());
    std::array<std::uint8_t, gfx::kPaletteSize * 3> pal{};
    for (int i = 0; i < gfx::kPaletteSize; ++i) {
        pal[static_cast<std::size_t>(i * 3 + 0)] = theme.colors[static_cast<std::size_t>(i)].r;
        pal[static_cast<std::size_t>(i * 3 + 1)] = theme.colors[static_cast<std::size_t>(i)].g;
        pal[static_cast<std::size_t>(i * 3 + 2)] = theme.colors[static_cast<std::size_t>(i)].b;
    }
    glBindTexture(GL_TEXTURE_2D, palette_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gfx::kPaletteSize, 1, GL_RGB, GL_UNSIGNED_BYTE, pal.data());

    // 1) Palette + persistence. Half-life up to 120 ms at persistence = 1.
    const int prev = current_;
    current_ ^= 1;
    float decay = 0.0f;
    if (crt.enabled && crt.persistence > 0.0f) {
        const double half_life = 0.12 * crt.persistence;
        decay = static_cast<float>(std::pow(0.5, std::clamp(dt_s, 0.0, 0.1) / half_life));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, persist_[static_cast<std::size_t>(current_)].fbo);
    glViewport(0, 0, canvas_w_, canvas_h_);
    glUseProgram(phosphor_prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, canvas_tex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, palette_tex_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, persist_[static_cast<std::size_t>(prev)].texture);
    glUniform1i(loc(phosphor_prog_, "uCanvas"), 0);
    glUniform1i(loc(phosphor_prog_, "uPalette"), 1);
    glUniform1i(loc(phosphor_prog_, "uPrevious"), 2);
    glUniform1f(loc(phosphor_prog_, "uDecay"), decay);
    glUniform2i(loc(phosphor_prog_, "uCanvasSize"), canvas_w_, canvas_h_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    const GLuint image = persist_[static_cast<std::size_t>(current_)].texture;

    // 2) Bloom at half resolution: bright-pass + horizontal, then vertical.
    glUseProgram(blur_prog_);
    glUniform1i(loc(blur_prog_, "uImage"), 0);
    glViewport(0, 0, canvas_w_ / 2, canvas_h_ / 2);
    glBindFramebuffer(GL_FRAMEBUFFER, bloom_[0].fbo);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image);
    glUniform2f(loc(blur_prog_, "uDirection"), 2.0f / static_cast<float>(canvas_w_), 0.0f);
    glUniform1f(loc(blur_prog_, "uThreshold"), 0.30f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, bloom_[1].fbo);
    glBindTexture(GL_TEXTURE_2D, bloom_[0].texture);
    glUniform2f(loc(blur_prog_, "uDirection"), 0.0f, 2.0f / static_cast<float>(canvas_h_));
    glUniform1f(loc(blur_prog_, "uThreshold"), 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 3) CRT pass into the letterboxed viewport (GL viewport origin is bottom-left).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(viewport.x, fb_h - viewport.y - viewport.h, viewport.w, viewport.h);
    glUseProgram(crt_prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom_[1].texture);
    glUniform1i(loc(crt_prog_, "uImage"), 0);
    glUniform1i(loc(crt_prog_, "uBloom"), 1);
    glUniform2f(loc(crt_prog_, "uCanvasSize"), static_cast<float>(canvas_w_), static_cast<float>(canvas_h_));
    glUniform2f(loc(crt_prog_, "uOutputSize"), static_cast<float>(viewport.w), static_cast<float>(viewport.h));
    glUniform1i(loc(crt_prog_, "uEnabled"), crt.enabled ? 1 : 0);
    glUniform1f(loc(crt_prog_, "uScanlines"), crt.scanlines);
    glUniform1f(loc(crt_prog_, "uMask"), crt.mask);
    glUniform1f(loc(crt_prog_, "uBloomAmount"), crt.bloom);
    glUniform1f(loc(crt_prog_, "uVignette"), crt.vignette);
    glUniform1f(loc(crt_prog_, "uCurvature"), crt.curvature);
    glUniform1f(loc(crt_prog_, "uBrightness"), crt.brightness);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
}

std::optional<gfx::Image> CrtRenderer::read_framebuffer(int fb_w, int fb_h) const {
    if (fb_w <= 0 || fb_h <= 0) {
        return std::nullopt;
    }
    gfx::Image img;
    img.width = fb_w;
    img.height = fb_h;
    img.channels = 3;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(fb_w) * static_cast<std::size_t>(fb_h) * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, fb_w, fb_h, GL_RGB, GL_UNSIGNED_BYTE, raw.data());
    img.pixels.resize(raw.size());
    const std::size_t stride = static_cast<std::size_t>(fb_w) * 3;
    for (int y = 0; y < fb_h; ++y) {
        std::copy_n(raw.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(fb_h - 1 - y) * stride),
                    stride, img.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * stride));
    }
    return img;
}

} // namespace phos::platform
