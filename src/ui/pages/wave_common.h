#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

#include "gfx/canvas.h"
#include "model/wavetable.h"
#include "ui/layout.h"
#include "ui/ui.h"
#include "ui/widgets.h"

// Shared by the WAVETABLE pages (4 HARMONICS, 6 WAVEDRAW, D WAVEFORM DISPLAY, 0 SPECTRAL DRAWING).
namespace phos::ui::wave {

// The current instrument if it is a WAVETABLE with data, else nullptr.
inline const model::Instrument* instrument(const Ui& ui) {
    const model::Instrument& inst = ui.app().workspace().doc().current_instrument();
    return inst.kind == model::InstrumentKind::Wavetable && inst.wavetable ? &inst : nullptr;
}

inline void draw_not_wave(gfx::Canvas& c, const Ui& ui) {
    const auto& doc = ui.app().workspace().doc();
    char buf[96];
    std::snprintf(buf, sizeof buf, "INSTRUMENT %02d IS A %s, NOT A WAVETABLE.", doc.current + 1,
                  model::kind_name(doc.current_instrument().kind));
    widgets::text(c, ui, 2, 6, buf, gfx::kInkDim);
    widgets::text(c, ui, 2, 8, "TYPE KIND WAVE (REPLACES ITS SETTINGS, UNDOABLE),", gfx::kInkDim);
    widgets::text(c, ui, 2, 9, "OR INST <N> TO PICK ANOTHER INSTRUMENT.", gfx::kInkDim);
}

// Replaces the current instrument's wavetable as one undoable step; `label` + time merge drags.
inline void commit(Ui& ui, model::WavetableData data, const std::string& label, bool merge = true) {
    model::Workspace& ws = ui.app().workspace();
    model::Instrument inst = ws.doc().current_instrument();
    inst.wavetable = std::make_shared<const model::WavetableData>(std::move(data));
    ws.set_instrument(ws.doc().current, inst, label, merge ? ui.app().time_s() : -10.0);
}

inline void commit_params(Ui& ui, const model::WavetableParams& w, const std::string& label) {
    model::Workspace& ws = ui.app().workspace();
    model::Instrument inst = ws.doc().current_instrument();
    inst.wave = w;
    ws.set_instrument(ws.doc().current, inst, label, ui.app().time_s());
}

// Oblique 3D view: `rows` polylines of `points` values (-1..1), back row first. Each row is
// shifted up-right by (dx, dy) and coloured by depth along the ramp inks.
inline void waterfall(gfx::Canvas& c, gfx::Rect area, int rows, int points, double dx, double dy, double amp,
                      const std::function<float(int, int)>& value) {
    if (rows <= 0 || points <= 1) {
        return;
    }
    const double width = area.w - dx * (rows - 1);
    const double base_y = area.y + area.h - amp - 2;
    std::array<gfx::Point, 128> line{};
    for (int r = rows - 1; r >= 0; --r) { // back to front so the front row is drawn last
        const double ox = area.x + dx * r;
        const double oy = base_y - dy * r;
        const int n = std::min(points, static_cast<int>(line.size()));
        for (int i = 0; i < n; ++i) {
            line[static_cast<std::size_t>(i)] = gfx::Point{static_cast<int>(ox + width * i / (n - 1)),
                                                           static_cast<int>(oy - value(r, i) * amp)};
        }
        const int ramp = rows > 1 ? 5 - r * 5 / (rows - 1) : 5;
        c.polyline(std::span<const gfx::Point>(line.data(), static_cast<std::size_t>(n)),
                   static_cast<std::uint8_t>(gfx::kInkRamp1 + ramp));
    }
}

} // namespace phos::ui::wave
