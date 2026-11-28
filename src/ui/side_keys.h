#pragma once

#include "gfx/canvas.h"
#include "ui/input.h"

// Touch / click keys in the strips beside the 4:3 canvas when the screen is wider (gfx::SideKeys,
// SIDEKEYS command, Page S): screen navigation on the left edge, cursor up / down and value change
// on the right. Pen positions are canvas coordinates: x < 0 is the left strip, x >= kCanvasW the
// right one. Up, down, + and - repeat while held.
namespace phos::ui {

class Ui;

enum class SideKey : int { Index, PagePrev, PageNext, Back, Escape, Up, Down, Plus, Minus, Enter };
inline constexpr int kSideKeyCount = 10;
inline constexpr int kSideKeysPerEdge = 5;

class SideKeys {
public:
    void set_width(int w);
    int width() const { return width_; }

    // Every frame. Presses and repeats keys; true while the pen is over a strip (the page ignores it).
    bool handle_pen(const InputState& in, Ui& ui);
    // `screen` holds the strips and the canvas, the canvas at x = width().
    void draw(gfx::Canvas& screen, const Ui& ui) const;

    // Key rectangle in canvas coordinates (left keys at negative x).
    gfx::Rect key_rect(SideKey key) const;
    // The key under a canvas position, or -1.
    int key_at(float x, float y) const;
    int held() const { return held_; }

    static constexpr double kRepeatDelay_s = 0.4;
    static constexpr double kRepeatInterval_s = 1.0 / 12.0;

private:
    int width_ = 0;
    int held_ = -1;
    int hover_ = -1;
    double next_repeat_s_ = 0.0;
};

} // namespace phos::ui
