// Page L — LOOP LIBRARY: eight loop slots, the selected slot's detail and waveform, and the VAPOR
// macro (docs/screens.md §3, PLAN.md §7.5).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/text.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

enum Area : int { kTable, kDetail, kVapor };
enum TableCol : int { kName, kBars, kSync, kQuant, kTrack, kTableCols };
enum DetailField : int { kGain, kLp, kRev, kScrew, kShift, kSlices, kBeats, kFrom, kClear, kDetailFields };
constexpr int kDetailRow1 = 4; // fields 0..3 on the first detail row, 4..8 on the second

constexpr int kTop = 3; // first slot row
constexpr int kZoneTable = 1000;
constexpr int kZonePlay = 1200;
constexpr int kZoneDetail = 1300;
constexpr int kZoneVapor = 1400;

struct Col {
    int col;
    int width;
};
constexpr std::array<Col, kTableCols> kTableAt = {{{4, 12}, {17, 2}, {26, 7}, {34, 4}, {39, 1}}};
constexpr int kBpmCol = 20;
constexpr int kBarCol = 42;
constexpr int kBarCols = 21;
// Detail fields: label column, value column, width, row.
struct Field {
    const char* label;
    int label_col;
    int col;
    int width;
    int row;
};
constexpr std::array<Field, kDetailFields> kDetailAt = {{
    {"GAIN", 2, 7, 5, 13},
    {"LP", 14, 17, 5, 13},
    {"REV", 24, 28, 3, 13},
    {"SCREW", 33, 39, 4, 13},
    {"SHIFT", 2, 8, 3, 14},
    {"SLICES", 13, 20, 2, 14},
    {"BEATS/BAR", 24, 34, 2, 14},
    {"FROM INST", 38, 48, 2, 14},
    {"CLEAR", 52, 52, 5, 14},
}};

std::string fmt(const char* f, double v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

class LoopsPage final : public Page {
public:
    std::string_view id() const override { return "L"; }
    std::string_view title() const override { return "LOOP LIBRARY"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type == InputEvent::Type::Text) {
            if (ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_') {
                change(ui, (ev.ch == '+' || ev.ch == '=') ? 1 : -1, false);
                return true;
            }
            return false;
        }
        const bool big = (ev.mods & kModShift) != 0;
        switch (ev.key) {
        case Key::Up:
            up(ui);
            return true;
        case Key::Down:
            down();
            return true;
        case Key::Left:
            if (area_ == kTable) {
                col_ = std::max(0, col_ - 1);
            } else if (area_ == kDetail) {
                det_ = std::max(det_ < kDetailRow1 ? 0 : kDetailRow1, det_ - 1);
            }
            return true;
        case Key::Right:
            if (area_ == kTable) {
                col_ = std::min(kTableCols - 1, col_ + 1);
            } else if (area_ == kDetail) {
                det_ = std::min(det_ < kDetailRow1 ? kDetailRow1 - 1 : kDetailFields - 1, det_ + 1);
            }
            return true;
        case Key::PageUp:
        case Key::PageDown:
            change(ui, ev.key == Key::PageUp ? 1 : -1, big);
            return true;
        case Key::Enter:
            activate(ui);
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (zone >= kZoneVapor) {
            area_ = kVapor;
            change(ui, wheel != 0 ? wheel : 0, false);
        } else if (zone >= kZoneDetail) {
            area_ = kDetail;
            det_ = std::clamp(zone - kZoneDetail, 0, kDetailFields - 1);
            if (wheel != 0) {
                change(ui, wheel, false);
            } else if (det_ == kRev || det_ == kFrom || det_ == kClear) {
                activate(ui);
            }
        } else if (zone >= kZonePlay) {
            area_ = kTable;
            row_ = std::clamp(zone - kZonePlay, 0, 7);
            toggle_play(ui);
        } else if (zone >= kZoneTable) {
            area_ = kTable;
            row_ = std::clamp((zone - kZoneTable) / 16, 0, 7);
            col_ = std::clamp((zone - kZoneTable) % 16, 0, kTableCols - 1);
            if (wheel != 0) {
                change(ui, wheel, false);
            }
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const bool fields = ui.focus() == Focus::Fields;
        const model::Song& song = ui.app().workspace().doc().song;
        const engine::LoopInfo info = ui.app().loop_info();
        const int bpm10 = song.sections[static_cast<std::size_t>(ui.app().status().section)].bpm10;

        widgets::small_text(c, ui, px(1), py(2) + 3, "#", gfx::kInkDim);
        widgets::small_text(c, ui, px(4), py(2) + 3, "NAME", gfx::kInkDim);
        widgets::small_text(c, ui, px(17), py(2) + 3, "BARS", gfx::kInkDim);
        widgets::small_text(c, ui, px(kBpmCol) + 4, py(2) + 3, "BPM", gfx::kInkDim);
        widgets::small_text(c, ui, px(26), py(2) + 3, "SYNC", gfx::kInkDim);
        widgets::small_text(c, ui, px(34), py(2) + 3, "QUANT", gfx::kInkDim);
        widgets::small_text(c, ui, px(39), py(2) + 3, "TRK", gfx::kInkDim);
        widgets::small_text(c, ui, px(kBarCol), py(2) + 3, "POSITION  (ENTER PLAYS)", gfx::kInkDim);

        for (int i = 0; i < model::kLoopSlots; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            const model::LoopSlot& l = song.loops[ii];
            const int row = kTop + i;
            const bool sel = row_ == i;
            const bool empty = !l.sample;
            widgets::text(c, ui, 1, row, std::to_string(i + 1), sel ? gfx::kInkBright : gfx::kInkDim);
            const engine::LoopState st = info.state[ii];
            if (st != engine::LoopState::Off) {
                const bool blink = std::fmod(ui.app().time_s(), 0.5) < 0.25;
                widgets::text(c, ui, 2, row, ">", st == engine::LoopState::Playing || blink ? gfx::kInkPlay : gfx::kInkDim);
            }
            const auto focused = [&](int col) { return fields && area_ == kTable && sel && col_ == col; };
            const auto zone = [&](int col) { return kZoneTable + i * 16 + col; };
            const auto& at = kTableAt;
            widgets::value_field(c, ui, at[kName].col, row, at[kName].width, fit(l.name, 12), focused(kName), zone(kName));
            widgets::value_field(c, ui, at[kBars].col, row, at[kBars].width, empty ? "--" : fmt("%2.0f", l.bars),
                                 focused(kBars), zone(kBars));
            widgets::text(c, ui, kBpmCol, row, empty ? "  ---" : fmt("%5.1f", l.loop_bpm()), gfx::kInkDim);
            widgets::value_field(c, ui, at[kSync].col, row, at[kSync].width, model::loop_sync_name(l.sync), focused(kSync),
                                 zone(kSync));
            widgets::value_field(c, ui, at[kQuant].col, row, at[kQuant].width, model::launch_quantize_name(l.quantize),
                                 focused(kQuant), zone(kQuant));
            widgets::value_field(c, ui, at[kTrack].col, row, at[kTrack].width, std::string(1, model::track_label(l.track)),
                                 focused(kTrack), zone(kTrack));
            const gfx::Rect bar = cells(kBarCol, row, kBarCols, 1);
            const gfx::Rect inner{bar.x + 1, bar.y + 3, bar.w - 2, bar.h - 6};
            c.rect(inner, empty ? gfx::kInkGrid : gfx::kInkDim);
            if (st == engine::LoopState::Playing) {
                const int w = static_cast<int>(std::clamp(info.phase[ii], 0.0f, 1.0f) * static_cast<float>(inner.w - 2));
                c.fill_rect(gfx::Rect{inner.x + 1, inner.y + 1, w, inner.h - 2}, gfx::kInkPlay);
            }
            if (!empty) {
                for (int b = 1; b < l.bars && l.bars <= 16; ++b) { // bar ticks
                    const int x = inner.x + b * inner.w / l.bars;
                    c.vline(x, inner.y + inner.h - 3, inner.y + inner.h - 1, gfx::kInkDim);
                }
            }
            ui.add_zone(bar, ZoneOwner::Page, kZonePlay + i);
        }

        // Detail of the selected slot.
        const model::LoopSlot& l = song.loops[static_cast<std::size_t>(row_)];
        char title[48];
        std::snprintf(title, sizeof title, "LOOP %d  %s", row_ + 1, fit(l.name, 12).c_str());
        widgets::frame(c, ui, gfx::Rect{2, py(12) + 6, kCanvasW - 4, py(26) - 2 - (py(12) + 6)}, title);
        const auto dfocus = [&](int f) { return fields && area_ == kDetail && det_ == f; };
        const std::array<std::string, kDetailFields> values = {
            fmt("%+5.1f", l.gain_db),
            l.lp_hz >= 19990.0f ? std::string("OPEN") : fmt("%5.0f", l.lp_hz),
            l.reverse ? "ON" : "OFF",
            fmt("%4.2f", l.screw),
            fmt("%+3.0f", l.shift),
            fmt("%2.0f", l.slices),
            fmt("%2.0f", l.beats_per_bar),
            fmt("%02.0f", from_ + 1),
            "CLEAR",
        };
        for (int f = 0; f < kDetailFields; ++f) {
            const Field& d = kDetailAt[static_cast<std::size_t>(f)];
            if (f != kClear) {
                widgets::text(c, ui, d.label_col, d.row, d.label, gfx::kInkDim);
            }
            widgets::value_field(c, ui, d.col, d.row, d.width, values[static_cast<std::size_t>(f)], dfocus(f), kZoneDetail + f);
        }
        std::string src = "NO SAMPLE: LOOP <N> LOAD <PATH>, FROM INST + ENTER, OR RESAMPLE";
        if (l.sample) {
            char buf[112];
            std::snprintf(buf, sizeof buf, "SOURCE %.2f S %s.  %s", l.sample->seconds(), l.sample->channels == 2 ? "STEREO" : "MONO",
                          l.sync == model::LoopSync::Varispeed ? "PITCH FOLLOWS THE TEMPO"
                          : l.sync == model::LoopSync::Stretch ? "PITCH KEPT (VARISPEED UNTIL THE CACHE IS READY)"
                                                               : "SLICES RETRIGGER ON THE GRID");
            src = buf;
        }
        widgets::small_text(c, ui, px(2), py(15) + 3, src, gfx::kInkDim);
        draw_wave(c, l, info.state[static_cast<std::size_t>(row_)] == engine::LoopState::Playing ? info.phase[static_cast<std::size_t>(row_)] : -1.0f,
                  bpm10);

        // VAPOR macro.
        const bool vfocus = fields && area_ == kVapor;
        widgets::text(c, ui, 1, 26, "VAPOR", vfocus ? gfx::kInkBright : gfx::kInkText);
        const gfx::Rect vb = cells(8, 26, 40, 1);
        const gfx::Rect vi{vb.x, vb.y + 2, vb.w, vb.h - 4};
        c.rect(vi, vfocus ? gfx::kInkBright : gfx::kInkDim);
        const int vw = (vi.w - 2) * song.vapor / model::kVaporMax;
        if (vw > 0) {
            c.fill_rect(gfx::Rect{vi.x + 1, vi.y + 1, vw, vi.h - 2}, gfx::kInkText);
        }
        ui.add_zone(vb, ZoneOwner::Page, kZoneVapor);
        widgets::text(c, ui, 49, 26, fmt("%3.0f", song.vapor), vfocus ? gfx::kInkBright : gfx::kInkText);
        widgets::small_text(c, ui, px(53), py(26) + 3, "SLOW DARK WET", gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(27) + 3,
                            "LOOPS LANE (TRACKTYPE LOOPS): C4..G4 = SLOT 1..8.   RESAMPLE: SECTION -> FREE SLOT",
                            gfx::kInkDim);
    }

private:
    void draw_wave(gfx::Canvas& c, const model::LoopSlot& l, float phase, int /*bpm10*/) const {
        const gfx::Rect r = cells(2, 17, 60, 8);
        c.rect(r, gfx::kInkGrid);
        if (!l.sample || l.sample->overview.min.empty()) {
            return;
        }
        const auto& ov = l.sample->overview;
        const int mid = r.y + r.h / 2;
        const float half = static_cast<float>(r.h / 2 - 2);
        const auto bins = ov.min.size();
        for (int x = 1; x < r.w - 1; ++x) {
            std::size_t b = static_cast<std::size_t>(x - 1) * bins / static_cast<std::size_t>(r.w - 2);
            if (l.reverse) {
                b = bins - 1 - b;
            }
            const int y0 = mid - static_cast<int>(std::clamp(ov.max[b], -1.0f, 1.0f) * half);
            const int y1 = mid - static_cast<int>(std::clamp(ov.min[b], -1.0f, 1.0f) * half);
            c.vline(r.x + x, y0, std::max(y0, y1), gfx::kInkText);
        }
        if (l.sync == model::LoopSync::Slice) {
            for (int s = 1; s < l.slices; ++s) {
                const int x = r.x + 1 + s * (r.w - 2) / l.slices;
                c.vline(x, r.y + 1, r.y + 4, gfx::kInkDim);
            }
        }
        for (int b = 1; b < l.bars; ++b) {
            c.vline(r.x + 1 + b * (r.w - 2) / l.bars, r.y + r.h - 5, r.y + r.h - 2, gfx::kInkDim);
        }
        if (phase >= 0.0f) {
            const int x = r.x + 1 + static_cast<int>(phase * static_cast<float>(r.w - 3));
            c.vline(x, r.y + 1, r.y + r.h - 2, gfx::kInkCursor);
        }
    }

    void up(Ui& ui) {
        if (area_ == kVapor) {
            area_ = kDetail;
            det_ = kDetailRow1;
        } else if (area_ == kDetail) {
            if (det_ >= kDetailRow1) {
                det_ = std::min(det_ - kDetailRow1, kDetailRow1 - 1);
            } else {
                area_ = kTable;
            }
        } else if (row_ == 0) {
            ui.set_focus(Focus::Command);
        } else {
            --row_;
        }
    }

    void down() {
        if (area_ == kTable) {
            if (row_ < model::kLoopSlots - 1) {
                ++row_;
            } else {
                area_ = kDetail;
                det_ = 0;
            }
        } else if (area_ == kDetail) {
            if (det_ < kDetailRow1) {
                det_ = std::min(det_ + kDetailRow1, kDetailFields - 1);
            } else {
                area_ = kVapor;
            }
        }
    }

    void toggle_play(Ui& ui) { ui.execute("LOOP " + std::to_string(row_ + 1) + " TOGGLE"); }

    void activate(Ui& ui) {
        if (area_ == kTable) {
            toggle_play(ui);
        } else if (area_ == kDetail && det_ == kFrom) {
            ui.execute("LOOP " + std::to_string(row_ + 1) + " FROM " + std::to_string(from_ + 1));
        } else if (area_ == kDetail && det_ == kClear) {
            ui.execute("LOOP " + std::to_string(row_ + 1) + " CLEAR");
        } else {
            change(ui, 1, false);
        }
    }

    void change(Ui& ui, int dir, bool big) {
        if (dir == 0) {
            return;
        }
        model::Workspace& ws = ui.app().workspace();
        model::Song s = ws.doc().song;
        const double now = ui.app().time_s();
        if (area_ == kVapor) {
            s.vapor = std::clamp(s.vapor + dir * (big ? 25 : 5), 0, model::kVaporMax);
            ws.set_song(s, "VAPOR", now);
            return;
        }
        model::LoopSlot& l = s.loops[static_cast<std::size_t>(row_)];
        const char* label = "LOOP";
        if (area_ == kTable) {
            switch (col_) {
            case kBars:
                l.bars = std::clamp(big ? (dir > 0 ? l.bars * 2 : l.bars / 2) : l.bars + dir, 1, 64);
                label = "LOOP BARS";
                break;
            case kSync:
                l.sync = static_cast<model::LoopSync>((static_cast<int>(l.sync) + 3 + dir) % 3);
                label = "LOOP SYNC";
                break;
            case kQuant:
                l.quantize = static_cast<model::LaunchQuantize>((static_cast<int>(l.quantize) + 3 + dir) % 3);
                label = "LOOP QUANT";
                break;
            case kTrack:
                l.track = (l.track + model::kTracks + dir) % model::kTracks;
                label = "LOOP TRACK";
                break;
            default:
                return; // NAME: Enter plays / stops
            }
        } else {
            switch (det_) {
            case kGain:
                l.gain_db = std::clamp(l.gain_db + static_cast<float>(dir) * (big ? 6.0f : 0.5f), -60.0f, 12.0f);
                label = "LOOP GAIN";
                break;
            case kLp:
                l.lp_hz = std::clamp(l.lp_hz * std::exp2(static_cast<float>(dir) / (big ? 1.0f : 6.0f)), 200.0f, 20000.0f);
                l.lp_hz = l.lp_hz > 19000.0f ? 20000.0f : std::round(l.lp_hz);
                label = "LOOP LP";
                break;
            case kRev:
                l.reverse = !l.reverse;
                label = "LOOP REVERSE";
                break;
            case kScrew:
                l.screw = std::clamp(std::round((l.screw + static_cast<float>(dir) * (big ? 0.1f : 0.01f)) * 100.0f) / 100.0f,
                                     0.5f, 1.0f);
                label = "LOOP SCREW";
                break;
            case kShift:
                l.shift = std::clamp(l.shift + static_cast<float>(dir * (big ? 12 : 1)), -24.0f, 24.0f);
                label = "LOOP SHIFT";
                break;
            case kSlices:
                l.slices = std::clamp(big ? (dir > 0 ? l.slices * 2 : l.slices / 2) : l.slices + dir, 2, 64);
                label = "LOOP SLICES";
                break;
            case kBeats:
                l.beats_per_bar = std::clamp(l.beats_per_bar + dir, 1, 16);
                label = "LOOP BEATS";
                break;
            case kFrom:
                from_ = (from_ + model::kInstruments + dir) % model::kInstruments;
                return; // chosen; ENTER takes the sample
            default:
                return;
            }
        }
        ws.set_song(s, label, now);
    }

    int area_ = kTable;
    int row_ = 0;
    int col_ = kName;
    int det_ = 0;
    int from_ = 0;
};

} // namespace

std::unique_ptr<Page> make_loops_page() { return std::make_unique<LoopsPage>(); }

} // namespace phos::ui
