#pragma once

#include <span>
#include <string_view>

#include "gfx/canvas.h"
#include "ui/input.h"

namespace phos::ui {

class Ui;

// One full screen. Pages are immediate-mode: draw() renders from current state, update() and the
// event hooks mutate it. Light-pen targets are registered during draw() via Ui::add_zone() and hit
// on the next frame.
class Page {
public:
    virtual ~Page() = default;

    virtual std::string_view id() const = 0;    // "1", "S", "H", "TESTCARD"
    virtual std::string_view title() const = 0; // shown in docs / help
    // Full-canvas pages (TESTCARD) draw without the screen chrome.
    virtual bool full_canvas() const { return false; }
    // Pages with editable fields accept FIELD focus (arrows, +/-).
    virtual bool has_fields() const { return false; }

    virtual void enter(Ui&) {}
    // "P3 OUT": a page with several views shows the one named (upper case). False if unknown.
    virtual bool show(std::string_view /*view*/) { return false; }
    virtual void update(const InputState&, Ui&) {}
    // Key event while this page has focus. Return true when consumed.
    virtual bool on_key(const InputEvent&, Ui&) { return false; }
    // Light pen pressed on one of this page's zones. `wheel` != 0 for scroll over a zone.
    virtual void on_zone(int /*zone*/, int /*wheel*/, Ui&) {}

    virtual void draw(gfx::Canvas& canvas, const Ui& ui) const = 0;
};

} // namespace phos::ui
