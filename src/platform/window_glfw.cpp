#include "platform/window_glfw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "gfx/viewport.h"

namespace phos::platform {

bool Window::monitors_changed_ = false;

namespace {

void glfw_error(int code, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", code, description); }

int translate_mods(int glfw_mods) {
    int m = 0;
    if (glfw_mods & GLFW_MOD_SHIFT) {
        m |= ui::kModShift;
    }
    if (glfw_mods & GLFW_MOD_CONTROL) {
        m |= ui::kModCtrl;
    }
    if (glfw_mods & GLFW_MOD_ALT) {
        m |= ui::kModAlt;
    }
    if (glfw_mods & GLFW_MOD_SUPER) {
        m |= ui::kModSuper;
    }
    return m;
}

// Maps a GLFW key to (Key, label char). Printable keys use their US-layout label.
ui::Key translate_key(int key, char& ch) {
    ch = 0;
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        ch = static_cast<char>('A' + (key - GLFW_KEY_A));
        return ui::Key::Char;
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        ch = static_cast<char>('0' + (key - GLFW_KEY_0));
        return ui::Key::Char;
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        return static_cast<ui::Key>(static_cast<int>(ui::Key::F1) + (key - GLFW_KEY_F1));
    }
    switch (key) {
    case GLFW_KEY_ESCAPE:
        return ui::Key::Escape;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        return ui::Key::Enter;
    case GLFW_KEY_TAB:
        return ui::Key::Tab;
    case GLFW_KEY_BACKSPACE:
        return ui::Key::Backspace;
    case GLFW_KEY_DELETE:
        return ui::Key::Delete;
    case GLFW_KEY_INSERT:
        return ui::Key::Insert;
    case GLFW_KEY_LEFT:
        return ui::Key::Left;
    case GLFW_KEY_RIGHT:
        return ui::Key::Right;
    case GLFW_KEY_UP:
        return ui::Key::Up;
    case GLFW_KEY_DOWN:
        return ui::Key::Down;
    case GLFW_KEY_HOME:
        return ui::Key::Home;
    case GLFW_KEY_END:
        return ui::Key::End;
    case GLFW_KEY_PAGE_UP:
        return ui::Key::PageUp;
    case GLFW_KEY_PAGE_DOWN:
        return ui::Key::PageDown;
    case GLFW_KEY_SPACE:
        return ui::Key::Space;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
        ch = '-';
        return ui::Key::Char;
    case GLFW_KEY_EQUAL:
        ch = '=';
        return ui::Key::Char;
    case GLFW_KEY_KP_ADD:
        ch = '+';
        return ui::Key::Char;
    case GLFW_KEY_COMMA:
        ch = ',';
        return ui::Key::Char;
    case GLFW_KEY_PERIOD:
        ch = '.';
        return ui::Key::Char;
    case GLFW_KEY_SLASH:
        ch = '/';
        return ui::Key::Char;
    case GLFW_KEY_SEMICOLON:
        ch = ';';
        return ui::Key::Char;
    case GLFW_KEY_LEFT_BRACKET:
        ch = '[';
        return ui::Key::Char;
    case GLFW_KEY_RIGHT_BRACKET:
        ch = ']';
        return ui::Key::Char;
    default:
        return ui::Key::None;
    }
}

Window* from(GLFWwindow* w) { return static_cast<Window*>(glfwGetWindowUserPointer(w)); }

} // namespace

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
    }
    if (glfw_ready_) {
        glfwTerminate();
    }
}

bool Window::create(const char* title, int width, int height, std::string* error) {
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) {
        *error = "failed to initialise GLFW";
        return false;
    }
    glfw_ready_ = true;
#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    windowed_w_ = width;
    windowed_h_ = height;
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        *error = "failed to create window (OpenGL 3.3 core required)";
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        *error = "failed to load OpenGL functions";
        return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &Window::key_callback);
    glfwSetCharCallback(window_, &Window::char_callback);
    glfwSetMouseButtonCallback(window_, &Window::mouse_button_callback);
    glfwSetScrollCallback(window_, &Window::scroll_callback);
    glfwSetMonitorCallback(&Window::monitor_callback);
    std::fprintf(stdout, "OpenGL %s / %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    return true;
}

bool Window::should_close() const { return glfwWindowShouldClose(window_) != 0; }

void Window::request_close() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

void Window::key_callback(GLFWwindow* w, int key, int /*scancode*/, int action, int mods) {
    ui::InputEvent ev;
    ev.type = ui::InputEvent::Type::Key;
    ev.key = translate_key(key, ev.ch);
    if (ev.key == ui::Key::None) {
        return;
    }
    ev.mods = translate_mods(mods);
    ev.repeat = action == GLFW_REPEAT;
    ev.release = action == GLFW_RELEASE;
    from(w)->pending_.push_back(ev);
}

void Window::char_callback(GLFWwindow* w, unsigned int codepoint) {
    if (codepoint < 0x20 || codepoint > 0x7E) {
        return; // ASCII only; the fonts have no other glyphs
    }
    ui::InputEvent ev;
    ev.type = ui::InputEvent::Type::Text;
    ev.ch = static_cast<char>(codepoint);
    from(w)->pending_.push_back(ev);
}

void Window::mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    Window* self = from(w);
    if (action == GLFW_PRESS) {
        self->pen_down_ = true;
        self->pen_pressed_ = true;
    } else if (action == GLFW_RELEASE) {
        self->pen_down_ = false;
        self->pen_released_ = true;
    }
}

void Window::scroll_callback(GLFWwindow* w, double /*dx*/, double dy) { from(w)->wheel_ += dy; }

void Window::monitor_callback(GLFWmonitor* /*monitor*/, int /*event*/) { monitors_changed_ = true; }

void Window::poll(ui::InputState& input, const gfx::Rect& viewport, int canvas_w, int canvas_h, float curvature) {
    glfwPollEvents();
    input.events.insert(input.events.end(), pending_.begin(), pending_.end());
    pending_.clear();
    input.pen_down = pen_down_;
    input.pen_pressed = pen_pressed_;
    input.pen_released = pen_released_;
    pen_pressed_ = pen_released_ = false;
    const int notches = static_cast<int>(std::trunc(wheel_));
    input.wheel = notches;
    wheel_ -= notches;
    input.time_s = glfwGetTime();

    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(window_, &mx, &my);
    int win_w = 0;
    int win_h = 0;
    glfwGetWindowSize(window_, &win_w, &win_h);
    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    input.pen_inside = false;
    const bool hovering = glfwGetWindowAttrib(window_, GLFW_HOVERED) != 0;
    if (hovering && win_w > 0 && win_h > 0) {
        // Cursor positions are in window coordinates; the viewport is in framebuffer pixels (HiDPI).
        const double fx = mx * fb_w / win_w;
        const double fy = my * fb_h / win_h;
        if (const auto pos = gfx::framebuffer_to_canvas(viewport, canvas_w, canvas_h, curvature, fx, fy)) {
            input.pen_inside = true;
            input.pen_x = pos->x;
            input.pen_y = pos->y;
        }
    }
    // The light-pen crosshair replaces the OS cursor over the canvas only.
    if (input.pen_inside != cursor_hidden_) {
        cursor_hidden_ = input.pen_inside;
        glfwSetInputMode(window_, GLFW_CURSOR, cursor_hidden_ ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
    }
}

void Window::swap() { glfwSwapBuffers(window_); }

void Window::framebuffer_size(int& w, int& h) const { glfwGetFramebufferSize(window_, &w, &h); }

double Window::time_s() const { return glfwGetTime(); }

std::vector<MonitorInfo> Window::monitors() const {
    std::vector<MonitorInfo> out;
    int count = 0;
    GLFWmonitor** list = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        MonitorInfo m;
        m.handle = list[i];
        const char* name = glfwGetMonitorName(list[i]);
        m.name = name ? name : "MONITOR " + std::to_string(i + 1);
        glfwGetMonitorPos(list[i], &m.x, &m.y);
        if (const GLFWvidmode* mode = glfwGetVideoMode(list[i])) {
            m.mode_w = mode->width;
            m.mode_h = mode->height;
            m.refresh_hz = mode->refreshRate;
        }
        out.push_back(m);
    }
    return out;
}

int Window::current_monitor() const {
    const auto list = monitors();
    if (list.empty()) {
        return -1;
    }
    if (GLFWmonitor* fs = glfwGetWindowMonitor(window_)) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].handle == fs) {
                return static_cast<int>(i);
            }
        }
    }
    int wx = 0;
    int wy = 0;
    int ww = 0;
    int wh = 0;
    glfwGetWindowPos(window_, &wx, &wy);
    glfwGetWindowSize(window_, &ww, &wh);
    int best = 0;
    long best_area = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& m = list[i];
        const long ox = std::max(0, std::min(wx + ww, m.x + m.mode_w) - std::max(wx, m.x));
        const long oy = std::max(0, std::min(wy + wh, m.y + m.mode_h) - std::max(wy, m.y));
        if (ox * oy > best_area) {
            best_area = ox * oy;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool Window::is_fullscreen() const { return glfwGetWindowMonitor(window_) != nullptr; }

void Window::place(int index, bool fullscreen) {
    const auto list = monitors();
    if (list.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(list.size()) - 1);
    const MonitorInfo& m = list[static_cast<std::size_t>(index)];
    if (fullscreen) {
        if (!is_fullscreen()) {
            glfwGetWindowSize(window_, &windowed_w_, &windowed_h_);
        }
        // Current mode's size and refresh: GLFW keeps the video mode as is ("windowed fullscreen").
        glfwSetWindowMonitor(window_, m.handle, 0, 0, m.mode_w, m.mode_h, m.refresh_hz);
        return;
    }
    int ax = m.x;
    int ay = m.y;
    int aw = m.mode_w;
    int ah = m.mode_h;
    glfwGetMonitorWorkarea(m.handle, &ax, &ay, &aw, &ah);
    const int w = std::min(windowed_w_, std::max(320, aw - 40));
    const int h = std::min(windowed_h_, std::max(240, ah - 60));
    glfwSetWindowMonitor(window_, nullptr, ax + (aw - w) / 2, ay + (ah - h) / 2, w, h, 0);
}

bool Window::take_monitors_changed() {
    const bool changed = monitors_changed_;
    monitors_changed_ = false;
    return changed;
}

} // namespace phos::platform
