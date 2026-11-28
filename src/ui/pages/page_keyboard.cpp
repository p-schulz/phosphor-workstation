// Page 3 — KEYBOARD CONTROL: MIDI inputs, input handling, CC learn and the latency report
// (docs/screens.md §3, PLAN.md §7.6).

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "core/text.h"
#include "gfx/palette.h"
#include "ui/layout.h"
#include "ui/pages/midi_out_view.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

constexpr int kPortRows = 6;
// Field indices in focus order: two per port row (enable, channel), then the settings.
enum Field : int {
    kPortFirst = 0,
    kTiming = kPortRows * 2,
    kVelocity,
    kBend,
    kKeyjazz,
    kOctave,
    kLearn,
    kReset,
    kPanic,
    kFieldCount
};
constexpr int zone_of(int field, int option = 0) { return field * 16 + option; }
constexpr int kZoneView = 2000; // + 0 inputs, + 1 outputs

constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};
constexpr std::array<std::string_view, 2> kTimings = {"IMMEDIATE", "STABLE"};
constexpr std::array<std::string_view, 4> kCurves = {"LINEAR", "SOFT", "HARD", "FIXED"};
constexpr std::array<std::string_view, 2> kLearnOps = {"LEARN", "CLEAR"};
constexpr std::array<std::string_view, 1> kResetOp = {"RESET"};
constexpr std::array<std::string_view, 1> kPanicOp = {"PANIC"};

constexpr int kLabelCol = 2;
constexpr int kEq = 11;
constexpr int kValueCol = kLabelCol + kEq + 2;
constexpr int kRightLabel = 36;
constexpr int kRightValue = kRightLabel + 9;

std::string channel_text(int ch) {
    if (ch < 0) {
        return "ALL";
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%3d", ch + 1);
    return buf;
}

class KeyboardPage final : public Page {
public:
    std::string_view id() const override { return "3"; }
    std::string_view title() const override { return "KEYBOARD CONTROL"; }
    bool has_fields() const override { return true; }

    bool show(std::string_view view) override {
        if (view == "IN" || view == "OUT") {
            view_ = view == "OUT" ? 1 : 0;
            return true;
        }
        return false;
    }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type == InputEvent::Type::Key && (ev.key == Key::PageUp || ev.key == Key::PageDown)) {
            view_ = 1 - view_; // inputs <-> outputs
            return true;
        }
        if (view_ == 1) {
            return out_.on_key(ev, ui);
        }
        if (ev.type == InputEvent::Type::Text) {
            if (ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_') {
                change(ui, (ev.ch == '+' || ev.ch == '=') ? 1 : -1);
                return true;
            }
            return false;
        }
        switch (ev.key) {
        case Key::Up:
            if (focus_ == 0) {
                ui.set_focus(Focus::Command);
            } else {
                focus_ = prev_field(ui, focus_);
            }
            return true;
        case Key::Down:
        case Key::Tab:
            focus_ = next_field(ui, focus_);
            return true;
        case Key::Left:
            change(ui, (ev.mods & kModShift) ? -10 : -1);
            return true;
        case Key::Right:
            change(ui, (ev.mods & kModShift) ? 10 : 1);
            return true;
        case Key::Enter:
            change(ui, 1);
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (zone == kZoneView || zone == kZoneView + 1) {
            view_ = zone - kZoneView;
            return;
        }
        if (zone >= MidiOutView::kZoneBase) {
            out_.on_zone(zone, wheel, ui);
            return;
        }
        const int field = zone / 16;
        const int option = zone % 16;
        if (field < 0 || field >= kFieldCount) {
            return;
        }
        focus_ = field;
        if (wheel != 0) {
            change(ui, wheel);
            return;
        }
        select(ui, field, option);
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const bool fields = ui.focus() == Focus::Fields;
        constexpr std::array<std::string_view, 2> kViews = {"IN", "OUT"};
        if (view_ == 1) {
            out_.draw(c, ui, fields);
            widgets::option_group(c, ui, 57, 3, kViews, 1, false, kZoneView);
            return;
        }
        auto focused = [&](int f) { return fields && focus_ == f; };
        draw_ports(c, ui, focused);
        widgets::option_group(c, ui, 57, 3, kViews, 0, false, kZoneView);
        draw_input(c, ui, focused);
        draw_latency(c, ui, focused);
    }

private:
    template <typename F> void draw_ports(gfx::Canvas& c, const Ui& ui, F focused) const {
        const MidiInfo& mi = ui.app().midi();
        widgets::frame(c, ui, gfx::Rect{2, py(2) + 6, kCanvasW - 4, py(10) - 3 - (py(2) + 6)}, "MIDI INPUTS (" + mi.api + ")");
        widgets::small_text(c, ui, px(2), py(3) + 2, "IN", gfx::kInkDim);
        widgets::small_text(c, ui, px(10), py(3) + 2, "NAME", gfx::kInkDim);
        widgets::small_text(c, ui, px(41), py(3) + 2, "CH", gfx::kInkDim);
        widgets::small_text(c, ui, px(47), py(3) + 2, "STATE", gfx::kInkDim);
        if (!mi.available) {
            widgets::text(c, ui, 10, 4, "NO MIDI (HEADLESS)", gfx::kInkDim);
            return;
        }
        if (mi.ports.empty()) {
            widgets::text(c, ui, 10, 4, "NO MIDI INPUTS FOUND", gfx::kInkDim);
        }
        for (int i = 0; i < kPortRows && i < static_cast<int>(mi.ports.size()); ++i) {
            const MidiPortState& p = mi.ports[static_cast<std::size_t>(i)];
            const int row = 4 + i;
            widgets::option_group(c, ui, 2, row, kOffOn, p.enabled ? 1 : 0, focused(kPortFirst + i * 2),
                                  zone_of(kPortFirst + i * 2));
            widgets::text(c, ui, 10, row, fit(to_upper(p.name), 29), p.present ? gfx::kInkText : gfx::kInkDim);
            widgets::value_field(c, ui, 40, row, 4, channel_text(p.channel), focused(kPortFirst + i * 2 + 1),
                                 zone_of(kPortFirst + i * 2 + 1));
            const std::string state = !p.error.empty() ? p.error
                                      : p.open        ? "OPEN"
                                      : !p.present    ? "MISSING"
                                                      : "OFF";
            widgets::small_text(c, ui, px(47), py(row) + 3, fit(state, 16),
                                !p.error.empty() ? gfx::kInkWarn : p.open ? gfx::kInkText : gfx::kInkDim);
        }
        if (static_cast<int>(mi.ports.size()) > kPortRows) {
            widgets::small_text(c, ui, px(47), py(9) + 3,
                                "+" + std::to_string(mi.ports.size() - kPortRows) + " MORE IN SETTINGS", gfx::kInkGrid);
        }
    }

    template <typename F> void draw_input(gfx::Canvas& c, const Ui& ui, F focused) const {
        const MidiConfig& cfg = ui.app().midi_config();
        const MidiInfo& mi = ui.app().midi();
        widgets::frame(c, ui, gfx::Rect{2, py(10) + 6, kCanvasW - 4, py(17) - 3 - (py(10) + 6)}, "INPUT");
        widgets::label(c, ui, kLabelCol, 11, "TIMING", kEq);
        widgets::option_group(c, ui, kValueCol, 11, kTimings, static_cast<int>(cfg.placement), focused(kTiming),
                              zone_of(kTiming));
        widgets::label(c, ui, kLabelCol, 12, "VELOCITY", kEq);
        widgets::option_group(c, ui, kValueCol, 12, kCurves, static_cast<int>(cfg.velocity_curve), focused(kVelocity),
                              zone_of(kVelocity));
        widgets::label(c, ui, kLabelCol, 13, "BEND RANGE", kEq);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%3d", cfg.bend_range);
        widgets::value_field(c, ui, kValueCol, 13, 4, buf, focused(kBend), zone_of(kBend));
        widgets::text(c, ui, kValueCol + 5, 13, "SEMITONES", gfx::kInkDim);
        widgets::label(c, ui, kLabelCol, 14, "KEYJAZZ", kEq);
        widgets::option_group(c, ui, kValueCol, 14, kOffOn, ui.keyjazz() ? 1 : 0, focused(kKeyjazz), zone_of(kKeyjazz));
        widgets::label(c, ui, kRightLabel, 13, "SOUND", 7);
        widgets::option_group(c, ui, kRightValue, 13, kPanicOp, -1, focused(kPanic), zone_of(kPanic));
        widgets::label(c, ui, kRightLabel, 14, "OCTAVE", 7);
        std::snprintf(buf, sizeof buf, "%2d", cfg.keyjazz_octave);
        widgets::value_field(c, ui, kRightValue, 14, 3, buf, focused(kOctave), zone_of(kOctave));
        widgets::label(c, ui, kLabelCol, 15, "MASTER CC", kEq);
        if (mi.learning) {
            std::snprintf(buf, sizeof buf, "MOVE A CONTROL");
        } else if (cfg.master_cc < 0) {
            std::snprintf(buf, sizeof buf, "--");
        } else {
            std::snprintf(buf, sizeof buf, "CC %d CH %s", cfg.master_cc,
                          cfg.master_cc_channel < 0 ? "ALL" : std::to_string(cfg.master_cc_channel + 1).c_str());
        }
        widgets::text(c, ui, kValueCol, 15, buf, mi.learning ? gfx::kInkBright : gfx::kInkText);
        widgets::option_group(c, ui, kRightValue, 15, kLearnOps, mi.learning ? 0 : -1, focused(kLearn), zone_of(kLearn));
        widgets::small_text(c, ui, px(kRightLabel), py(11) + 3, "LAST CC", gfx::kInkDim);
        widgets::small_text(c, ui, px(kRightLabel + 6), py(11) + 3, mi.last_cc.empty() ? "--" : mi.last_cc,
                            gfx::kInkText);
    }

    template <typename F> void draw_latency(gfx::Canvas& c, const Ui& ui, F focused) const {
        const MidiInfo& mi = ui.app().midi();
        const AudioInfo& ai = ui.app().audio();
        const auto& l = mi.latency;
        widgets::frame(c, ui, gfx::Rect{2, py(17) + 6, kCanvasW - 4, py(29) - 3 - (py(17) + 6)}, "LATENCY REPORT");
        char buf[112];
        const unsigned rate = ai.status.sample_rate;
        const unsigned block = ai.meters.last_block ? ai.meters.last_block : ai.status.buffer_frames;
        std::snprintf(buf, sizeof buf, "BLOCK %u FR @ %u HZ = %.2f MS   TIMING %s", block, rate,
                      rate ? 1000.0 * block / rate : 0.0,
                      ui.app().midi_config().placement == engine::Placement::Stable ? "STABLE" : "IMMEDIATE");
        widgets::small_text(c, ui, px(2), py(18) + 2, buf, gfx::kInkText);
        widgets::option_group(c, ui, 57, 18, kResetOp, -1, focused(kReset), zone_of(kReset));
        std::snprintf(buf, sizeof buf, "EVENTS %llu  AVG %.2f MS  MIN %.2f  MAX %.2f  JITTER %.2f MS",
                      static_cast<unsigned long long>(l.count), l.avg_ms, l.min_ms, l.max_ms, l.jitter_ms());
        widgets::small_text(c, ui, px(2), py(19) + 2, buf, gfx::kInkText);
        const auto& m = mi.meters;
        std::snprintf(buf, sizeof buf, "NOTES %llu  XRUNS %llu  LOST %llu  EARLY %llu  LATE %llu",
                      static_cast<unsigned long long>(m.note_ons), static_cast<unsigned long long>(ai.meters.xruns),
                      static_cast<unsigned long long>(m.queue_overflow),
                      static_cast<unsigned long long>(m.schedule_overflow), static_cast<unsigned long long>(m.late));
        widgets::small_text(c, ui, px(2), py(20) + 2, buf, gfx::kInkDim);

        // Histogram of scheduling delay, 0.25 ms per bar; the last bar collects >= 5.75 ms.
        constexpr int kBarW = 16;
        const int x0 = px(4);
        const int base = py(27) - 2;
        const int max_h = py(27) - 2 - (py(21) + 4);
        std::uint64_t peak = 1;
        for (const auto n : l.histogram) {
            peak = std::max(peak, n);
        }
        for (int b = 0; b < engine::LatencyStats::kBuckets; ++b) {
            const auto n = l.histogram[static_cast<std::size_t>(b)];
            const int x = x0 + b * kBarW;
            c.hline(x, x + kBarW - 3, base, gfx::kInkGrid);
            if (n == 0) {
                continue;
            }
            const int h = std::max(1, static_cast<int>(static_cast<double>(n) / static_cast<double>(peak) * max_h));
            c.fill_rect(gfx::Rect{x, base - h, kBarW - 2, h}, gfx::kInkText);
        }
        for (int ms = 0; ms <= 6; ++ms) {
            const int x = x0 + ms * 4 * kBarW;
            c.vline(x, base + 1, base + 3, gfx::kInkDim);
            widgets::small_text(c, ui, x - 2, py(27) + 3, std::to_string(ms), gfx::kInkDim);
        }
        widgets::small_text(c, ui, x0 + 6 * 4 * kBarW + 10, py(27) + 3, "MS", gfx::kInkDim);
    }

    int port_rows(const Ui& ui) const { return std::min(kPortRows, static_cast<int>(ui.app().midi().ports.size())); }
    bool usable(const Ui& ui, int f) const { return f >= kTiming || f / 2 < port_rows(ui); }
    int next_field(const Ui& ui, int f) const {
        do {
            ++f;
        } while (f < kFieldCount && !usable(ui, f));
        return f < kFieldCount ? f : focus_;
    }
    int prev_field(const Ui& ui, int f) const {
        do {
            --f;
        } while (f >= 0 && !usable(ui, f));
        return f >= 0 ? f : focus_;
    }

    void select(Ui& ui, int field, int option) {
        if (field == kKeyjazz) {
            ui.execute(option == 1 ? "KEYJAZZ ON" : "KEYJAZZ OFF");
            return;
        }
        if (field == kLearn) {
            ui.execute(option == 0 ? "MIDI LEARN" : "MIDI LEARN CLEAR");
            return;
        }
        if (field == kReset) {
            ui.execute("MIDI RESET");
            return;
        }
        if (field == kPanic) {
            ui.execute("PANIC");
            return;
        }
        MidiConfig cfg = ui.app().midi_config();
        if (field == kTiming) {
            cfg.placement = static_cast<engine::Placement>(std::clamp(option, 0, 1));
        } else if (field == kVelocity) {
            cfg.velocity_curve = static_cast<engine::VelocityCurve>(std::clamp(option, 0, 3));
        } else if (field < kTiming && field % 2 == 0) {
            if (MidiInputSetting* s = port_setting(ui, cfg, field / 2)) {
                s->enabled = option == 1;
            }
        } else {
            return; // numeric fields: focus only
        }
        ui.app().set_midi_config(cfg);
    }

    MidiInputSetting* port_setting(const Ui& ui, MidiConfig& cfg, int row) const {
        const auto& ports = ui.app().midi().ports;
        return row < static_cast<int>(ports.size()) ? cfg.find(ports[static_cast<std::size_t>(row)].name) : nullptr;
    }

    void change(Ui& ui, int delta) {
        const int dir = delta > 0 ? 1 : -1;
        MidiConfig cfg = ui.app().midi_config();
        switch (focus_) {
        case kTiming:
            cfg.placement = static_cast<engine::Placement>((static_cast<int>(cfg.placement) + 2 + dir) % 2);
            break;
        case kVelocity:
            cfg.velocity_curve = static_cast<engine::VelocityCurve>((static_cast<int>(cfg.velocity_curve) + 4 + dir) % 4);
            break;
        case kBend:
            cfg.bend_range = std::clamp(cfg.bend_range + delta, 0, 24);
            break;
        case kOctave:
            cfg.keyjazz_octave = std::clamp(cfg.keyjazz_octave + dir, 0, 8);
            break;
        case kKeyjazz:
            ui.execute("KEYJAZZ");
            return;
        case kLearn:
            ui.execute("MIDI LEARN");
            return;
        case kReset:
            ui.execute("MIDI RESET");
            return;
        case kPanic:
            ui.execute("PANIC");
            return;
        default: {
            MidiInputSetting* s = port_setting(ui, cfg, focus_ / 2);
            if (!s) {
                return;
            }
            if (focus_ % 2 == 0) {
                s->enabled = !s->enabled;
            } else {
                s->channel = (s->channel + 1 + 17 + dir) % 17 - 1; // ALL, 1..16, wrapping
            }
            break;
        }
        }
        ui.app().set_midi_config(cfg);
    }

    int focus_ = kTiming;
    int view_ = 0; // 0 inputs, 1 outputs (M8)
    MidiOutView out_;
};

} // namespace

std::unique_ptr<Page> make_keyboard_page() { return std::make_unique<KeyboardPage>(); }

} // namespace phos::ui
