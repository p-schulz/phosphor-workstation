#pragma once

#include "gfx/canvas.h"
#include "ui/input.h"
#include "ui/ui.h"

// Page R, AUTO view (M8): one CC automation lane of the current track in the current pattern as a
// bar per step. The light pen draws values (the strip under the chart clears steps); the keyboard
// moves the step cursor (LEFT / RIGHT), sets values (UP / DOWN, SHIFT = 16) and clears (DEL);
// PAGE UP / DOWN choose the controller. `PR AUTO <cc>` opens it on a controller, `PR NOTES` leaves.
namespace phos::ui {

class AutomationView {
public:
    // Geometry shared with the NOTES grid of Page R.
    static constexpr int kGridX = 104;
    static constexpr int kGridW = 384;
    static constexpr int kZoneLane = 9500; // + controller (lane list on the left)

    void update(const InputState& in, Ui& ui, int view, int visible);
    bool on_key(const InputEvent& ev, Ui& ui, int& step, bool big);
    bool on_zone(int zone);
    void draw(gfx::Canvas& c, const Ui& ui, int view, int visible, int step, bool focused) const;

    int cc = 7;

private:
    void set(Ui& ui, int step, int value, const char* label) const;
    bool drawing_ = false;
    int last_step_ = 0;
    int last_value_ = 0;
};

const char* cc_name(int cc); // "VOLUME", "PAN", ... or "" for an unnamed controller

} // namespace phos::ui
