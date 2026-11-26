#include "ui/pages/automation_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "seq/timing.h"
#include "ui/layout.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

constexpr int kChartTop = py(7);
constexpr int kChartH = py(21) - py(7) - 2; // rows 7..20
constexpr int kClearTop = py(21);           // row 21: drawing here clears steps

} // namespace

const char* cc_name(int cc) {
    switch (cc) {
    case 1:
        return "MOD WHEEL";
    case 7:
        return "VOLUME";
    case 10:
        return "PAN";
    case 11:
        return "EXPRESSION";
    case 64:
        return "SUSTAIN";
    case 71:
        return "RESONANCE";
    case 74:
        return "CUTOFF";
    default:
        return "";
    }
}

void AutomationView::set(Ui& ui, int step, int value, const char* label) const {
    model::Workspace& ws = ui.app().workspace();
    model::Song s = ws.doc().song;
    model::Pattern& p = s.pattern();
    if (step < 0 || step >= p.steps) {
        return;
    }
    auto& lane = p.lane_for(s.current_track, cc);
    lane.values[static_cast<std::size_t>(step)] = static_cast<std::int16_t>(std::clamp(value, -1, 127));
    ws.set_song(std::move(s), label, ui.app().time_s());
}

void AutomationView::update(const InputState& in, Ui& ui, int view, int visible) {
    const gfx::Rect area{kGridX, kChartTop, kGridW, kClearTop + kCellH - kChartTop};
    if (!in.pen_down || !in.pen_inside || !area.contains(static_cast<int>(in.pen_x), static_cast<int>(in.pen_y))) {
        drawing_ = false;
        return;
    }
    const int cw = kGridW / std::max(1, visible);
    const int step = view + std::clamp(static_cast<int>(in.pen_x - kGridX) / cw, 0, visible - 1);
    const bool clear = static_cast<int>(in.pen_y) >= kClearTop;
    const int value = clear ? -1
                            : static_cast<int>(std::lround(127.0 * std::clamp(1.0 - (in.pen_y - kChartTop) / kChartH, 0.0, 1.0)));
    // Join to the previous pen position so fast strokes leave no gaps.
    const int from = drawing_ ? last_step_ : step;
    const int from_value = drawing_ ? last_value_ : value;
    model::Workspace& ws = ui.app().workspace();
    model::Song s = ws.doc().song;
    model::Pattern& p = s.pattern();
    auto& lane = p.lane_for(s.current_track, cc);
    const int lo = std::min(from, step);
    const int hi = std::max(from, step);
    for (int k = lo; k <= hi && k < p.steps; ++k) {
        const double t = hi == lo ? 1.0 : static_cast<double>(k - from) / (step - from);
        const int v = clear || from_value < 0 ? value : static_cast<int>(std::lround(from_value + t * (value - from_value)));
        lane.values[static_cast<std::size_t>(k)] = static_cast<std::int16_t>(v);
    }
    ws.set_song(std::move(s), "AUTOMATION", ui.app().time_s());
    drawing_ = true;
    last_step_ = step;
    last_value_ = value;
}

bool AutomationView::on_key(const InputEvent& ev, Ui& ui, int& step, bool big) {
    const model::Song& song = ui.app().workspace().doc().song;
    const model::AutomationLane* lane = song.pattern().lane(song.current_track, cc);
    const int cur = lane && step < static_cast<int>(lane->values.size()) ? lane->values[static_cast<std::size_t>(step)] : -1;
    switch (ev.key) {
    case Key::Left:
    case Key::Right:
        step = std::clamp(step + (ev.key == Key::Left ? -1 : 1) * (big ? 4 : 1), 0, song.pattern().steps - 1);
        return true;
    case Key::Up:
    case Key::Down: {
        const int base = cur < 0 ? 64 : cur;
        set(ui, step, std::clamp(base + (ev.key == Key::Up ? 1 : -1) * (big ? 16 : 1), 0, 127), "AUTOMATION");
        return true;
    }
    case Key::Delete:
    case Key::Backspace:
        set(ui, step, -1, "AUTOMATION");
        return true;
    case Key::PageUp:
    case Key::PageDown:
        cc = std::clamp(cc + (ev.key == Key::PageUp ? 1 : -1), 0, 127);
        return true;
    default:
        return false;
    }
}

bool AutomationView::on_zone(int zone) {
    if (zone < kZoneLane || zone >= kZoneLane + 128) {
        return false;
    }
    cc = zone - kZoneLane;
    return true;
}

void AutomationView::draw(gfx::Canvas& c, const Ui& ui, int view, int visible, int step, bool focused) const {
    const model::Song& song = ui.app().workspace().doc().song;
    const model::Pattern& p = song.pattern();
    const int cw = kGridW / std::max(1, visible);
    const int t = song.current_track;
    char buf[96];
    std::snprintf(buf, sizeof buf, "AUTO  TRACK %c  CC %d %s", model::track_label(t), cc, cc_name(cc));
    widgets::text(c, ui, 0, 6, buf, gfx::kInkBright);
    widgets::small_text(c, ui, px(38), py(6) + 3, "PG UP/DN: CC   PR NOTES: GRID", gfx::kInkDim);

    // Lanes of this track in this pattern, and the common controllers.
    std::set<int> lanes = {1, 7, 10, 11, 74, cc};
    for (const auto& l : p.automation) {
        if (l.track == t) {
            lanes.insert(l.cc);
        }
    }
    int row = 7;
    for (const int k : lanes) {
        if (row > 20) {
            break;
        }
        const bool has = p.lane(t, k) != nullptr;
        std::snprintf(buf, sizeof buf, "%c%3d %s", has ? '*' : ' ', k, cc_name(k));
        widgets::small_text(c, ui, 2, py(row) + 3, std::string(buf).substr(0, 16), k == cc ? gfx::kInkBright : gfx::kInkDim);
        ui.add_zone(gfx::Rect{0, py(row), kGridX - 4, kCellH}, ZoneOwner::Page, kZoneLane + k);
        ++row;
    }

    // Chart: 0 / 64 / 127 guides, beat lines, a bar per step.
    const gfx::Rect chart{kGridX - 1, kChartTop - 1, kGridW + 2, kChartH + 2};
    c.rect(chart, gfx::kInkDim);
    for (const int v : {32, 64, 96}) {
        const int y = kChartTop + kChartH - v * kChartH / 127;
        c.dotted_line(kGridX, y, kGridX + kGridW - 1, y, 4, v == 64 ? gfx::kInkDim : gfx::kInkGrid);
    }
    const auto beat = static_cast<std::uint32_t>(seq::beat_ticks(song.section().sig_den));
    const model::AutomationLane* lane = p.lane(t, cc);
    for (int k = view; k < view + visible && k < p.steps; ++k) {
        const int x = kGridX + (k - view) * cw;
        if (static_cast<std::uint32_t>(k) * p.step_ticks() % beat == 0) {
            c.dotted_line(x, kChartTop, x, kChartTop + kChartH - 1, 3, gfx::kInkGrid);
            widgets::small_text(c, ui, x + 1, py(5) + 3, std::to_string(k + 1), gfx::kInkDim);
        }
        const int v = lane && k < static_cast<int>(lane->values.size()) ? lane->values[static_cast<std::size_t>(k)] : -1;
        if (v >= 0) {
            const int h = std::max(1, v * kChartH / 127);
            c.fill_rect(gfx::Rect{x + 1, kChartTop + kChartH - h, std::max(1, cw - 2), h}, gfx::kInkLevelLow);
        } else {
            c.set(x + cw / 2, kChartTop + kChartH - 1, gfx::kInkGrid);
        }
    }
    if (focused && step >= view && step < view + visible) {
        c.rect(gfx::Rect{kGridX + (step - view) * cw, kChartTop - 1, cw, kChartH + 2}, gfx::kInkCursor);
    }
    c.stipple_rect(gfx::Rect{kGridX, kClearTop + 2, kGridW, kCellH - 4}, gfx::kInkShade);
    widgets::small_text(c, ui, px(1), kClearTop + 3, "DRAW = CLEAR", gfx::kInkDim);
    const int cur = lane && step < static_cast<int>(lane->values.size()) ? lane->values[static_cast<std::size_t>(step)] : -1;
    if (cur >= 0) {
        std::snprintf(buf, sizeof buf, "STEP %03d  CC %d = %d", step + 1, cc, cur);
    } else {
        std::snprintf(buf, sizeof buf, "STEP %03d  CC %d: NO CHANGE (UP/DOWN SETS, DEL CLEARS)", step + 1, cc);
    }
    widgets::text(c, ui, 1, 22, buf, gfx::kInkText);
}

} // namespace phos::ui
