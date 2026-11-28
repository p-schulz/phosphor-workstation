#pragma once

#include <string>
#include <vector>

#include "gfx/canvas.h"
#include "ui/input.h"

struct GLFWwindow;
struct GLFWmonitor;

namespace phos::platform {

struct MonitorInfo {
    std::string name;
    int x = 0; // virtual desktop position
    int y = 0;
    int mode_w = 0; // current video mode
    int mode_h = 0;
    int refresh_hz = 0;
    GLFWmonitor* handle = nullptr;
};

// GLFW window + GL context, input translation and monitor placement. Fullscreen is always
// borderless at the monitor's current video mode; video modes are never switched.
class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const char* title, int width, int height, std::string* error);

    bool should_close() const;
    void request_close();
    // Collects events since the last call into `input`. The pen position is mapped through the
    // current letterbox `viewport` (framebuffer pixels) and CRT curvature into canvas pixels.
    void poll(ui::InputState& input, const gfx::Rect& viewport, int canvas_w, int canvas_h, float curvature);
    void swap();

    void framebuffer_size(int& w, int& h) const;
    double time_s() const;

    std::vector<MonitorInfo> monitors() const;
    int current_monitor() const; // index into monitors(), -1 if none
    bool is_fullscreen() const;
    // Moves the window to monitor `index`: borderless fullscreen, or a centered window.
    void place(int index, bool fullscreen);
    // Windowed only: keeps the height and sets the width to height * aspect. False in fullscreen.
    bool set_aspect(double aspect);

    // True once after monitors were connected or disconnected.
    bool take_monitors_changed();

private:
    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow* w, unsigned int codepoint);
    static void mouse_button_callback(GLFWwindow* w, int button, int action, int mods);
    static void scroll_callback(GLFWwindow* w, double dx, double dy);
    static void monitor_callback(GLFWmonitor* monitor, int event);

    GLFWwindow* window_ = nullptr;
    bool glfw_ready_ = false;
    std::vector<ui::InputEvent> pending_;
    bool pen_down_ = false;
    bool pen_pressed_ = false;
    bool pen_released_ = false;
    double wheel_ = 0.0;
    int windowed_w_ = 1024;
    int windowed_h_ = 768;
    bool cursor_hidden_ = false;
    static bool monitors_changed_;
};

} // namespace phos::platform
