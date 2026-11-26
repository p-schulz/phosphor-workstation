#pragma once

#include "gfx/canvas.h"
#include "ui/input.h"
#include "ui/ui.h"

// Page 3, OUT view (M8, PLAN.md §7.7): MIDI outputs (on / off, clock), the output settings
// (MIDI OUT, THRU, OFFSET, LATENCY COMP) and where each of the 16 tracks plays (INT / EXT /
// BOTH, port, channel, program). Port, channel and program belong to the project (undoable);
// the rest are settings.
namespace phos::ui {

class MidiOutView {
public:
    static constexpr int kZoneBase = 3000; // + field * 4 + option

    bool on_key(const InputEvent& ev, Ui& ui);
    void on_zone(int zone, int wheel, Ui& ui);
    void draw(gfx::Canvas& c, const Ui& ui, bool fields) const;

private:
    void change(Ui& ui, int field, int dir) const;
    int focus_ = 0;
};

} // namespace phos::ui
