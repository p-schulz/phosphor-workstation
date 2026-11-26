#pragma once

#include <array>
#include <cstdio>
#include <string>
#include <string_view>

#include "model/workspace.h"
#include "ui/layout.h"
#include "ui/ui.h"
#include "ui/widgets.h"

// Page C views (docs/screens.md §3): the Game Boy view and the SNES view of the current instrument.
namespace phos::ui::chip {

// A row of option fields / numbers shared by both views: label, column, row, width.
struct FieldPos {
    std::string_view label;
    int label_col;
    int col;
    int row;
    int width;
};

inline void field(gfx::Canvas& c, const Ui& ui, const FieldPos& f, const std::string& value, bool focused, int zone) {
    widgets::text(c, ui, f.label_col, f.row, f.label, gfx::kInkDim);
    widgets::value_field(c, ui, f.col, f.row, f.width, value, focused, zone);
}

inline std::string num(int v, const char* fmt = "%d") {
    char buf[16];
    std::snprintf(buf, sizeof buf, fmt, v);
    return buf;
}

// Commit the current instrument (undoable; repeats of `label` merge within a second).
inline void commit(Ui& ui, const model::Instrument& inst, const std::string& label) {
    model::Workspace& ws = ui.app().workspace();
    ws.set_instrument(ws.doc().current, inst, label, ui.app().time_s());
}

class GbView {
public:
    void update(const InputState& in, Ui& ui);
    bool on_key(const InputEvent& ev, Ui& ui);
    void on_zone(int zone, int wheel, Ui& ui);
    void draw(gfx::Canvas& c, const Ui& ui) const;
    int focus = 0;

private:
    void change(Ui& ui, int dir, bool big);
    int wave_ = 0;  // wave shape shown in the grid
    int macro_ = 0; // macro lane shown
};

class SnesView {
public:
    void update(const InputState& in, Ui& ui);
    bool on_key(const InputEvent& ev, Ui& ui);
    void on_zone(int zone, int wheel, Ui& ui);
    void draw(gfx::Canvas& c, const Ui& ui) const;
    int focus = 0;

private:
    void change(Ui& ui, int dir, bool big);
    void reencode(Ui& ui, model::Instrument inst, const std::string& label);
};

} // namespace phos::ui::chip
