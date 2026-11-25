#pragma once

#include <cstdint>
#include <vector>

namespace phos::ui {

enum class Key : std::uint8_t {
    None,
    Escape,
    Enter,
    Tab,
    Backspace,
    Delete,
    Insert,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    Space,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Char, // printable key; KeyEvent::ch holds its uppercase US-layout label ('A', '1', '=', ...)
};

inline constexpr int kModShift = 1;
inline constexpr int kModCtrl = 2;
inline constexpr int kModAlt = 4;
inline constexpr int kModSuper = 8;

// One ordered stream of keyboard events: key presses (incl. repeats) interleaved with the
// characters they produce, in the order the OS delivered them.
struct InputEvent {
    enum class Type : std::uint8_t { Key, Text };
    Type type = Type::Key;
    Key key = Key::None;
    int mods = 0;
    char ch = 0; // Key::Char label, or the typed ASCII character for Type::Text
    bool repeat = false;
};

struct InputState {
    std::vector<InputEvent> events;

    // Light pen (mouse) in canvas pixels.
    bool pen_inside = false;
    float pen_x = 0.0f;
    float pen_y = 0.0f;
    bool pen_down = false;
    bool pen_pressed = false;  // went down this frame
    bool pen_released = false; // went up this frame
    int wheel = 0;             // +up / -down notches this frame

    double time_s = 0.0;

    void begin_frame() {
        events.clear();
        pen_pressed = false;
        pen_released = false;
        wheel = 0;
    }
};

} // namespace phos::ui
