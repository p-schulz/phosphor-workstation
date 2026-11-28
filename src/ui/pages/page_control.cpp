// Page 7 — CONTROL PARAMETERS: SAMPLER parameters of the current instrument in three columns, with
// the amp and filter envelopes drawn below (docs/screens.md §3, PLAN.md §7.3). Every change is an
// undoable step; repeated changes of one field merge.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>

#include "core/notes.h"
#include "core/text.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

using model::SamplerParams;

enum class Step { Add, Times, Note };

// One editable parameter. get/set work on the instrument's SamplerParams; `labels` makes it a
// choice field (value = index).
struct Field {
    std::string_view label;
    int col;
    int row;
    float (*get)(const SamplerParams&);
    void (*set)(SamplerParams&, float);
    float lo, hi, step, big;
    Step mode;
    const char* format; // printf for numbers ("%5.1f"); nullptr for notes and choices
    std::span<const std::string_view> labels;
};

constexpr std::array<std::string_view, 3> kInterp = {"DROP", "LIN", "HERM"};
constexpr std::array<std::string_view, 3> kLoop = {"OFF", "FWD", "PING"};
constexpr std::array<std::string_view, 4> kFilter = {"OFF", "LP", "HP", "BP"};

constexpr int kC1 = 1;
constexpr int kC2 = 22;
constexpr int kC3 = 43;

// clang-format off
const std::array<Field, 25> kFields = {{
    {"VOLUME DB", kC1, 5, [](const SamplerParams& p) { return p.volume_db; }, [](SamplerParams& p, float v) { p.volume_db = v; }, -60, 12, 0.5f, 6, Step::Add, "%5.1f", {}},
    {"PAN", kC1, 6, [](const SamplerParams& p) { return p.pan * 100.0f; }, [](SamplerParams& p, float v) { p.pan = v / 100.0f; }, -100, 100, 5, 25, Step::Add, "%5.0f", {}},
    {"ROOT NOTE", kC1, 7, [](const SamplerParams& p) { return static_cast<float>(p.root_note); }, [](SamplerParams& p, float v) { p.root_note = static_cast<int>(v); }, 0, 127, 1, 12, Step::Note, nullptr, {}},
    {"FINE CENT", kC1, 8, [](const SamplerParams& p) { return p.fine_cents; }, [](SamplerParams& p, float v) { p.fine_cents = v; }, -100, 100, 1, 10, Step::Add, "%5.0f", {}},
    {"INTERP", kC1, 9, [](const SamplerParams& p) { return static_cast<float>(p.interp); }, [](SamplerParams& p, float v) { p.interp = static_cast<model::Interp>(v); }, 0, 2, 1, 1, Step::Add, nullptr, kInterp},
    {"VEL>AMP %", kC1, 10, [](const SamplerParams& p) { return p.vel_to_amp * 100.0f; }, [](SamplerParams& p, float v) { p.vel_to_amp = v / 100.0f; }, 0, 100, 5, 25, Step::Add, "%5.0f", {}},

    {"ATTACK MS", kC2, 5, [](const SamplerParams& p) { return p.amp.attack_ms; }, [](SamplerParams& p, float v) { p.amp.attack_ms = v; }, 0.5f, 10000, 1.12f, 2, Step::Times, "%5.0f", {}},
    {"DECAY MS", kC2, 6, [](const SamplerParams& p) { return p.amp.decay_ms; }, [](SamplerParams& p, float v) { p.amp.decay_ms = v; }, 1, 20000, 1.12f, 2, Step::Times, "%5.0f", {}},
    {"SUSTAIN %", kC2, 7, [](const SamplerParams& p) { return p.amp.sustain * 100.0f; }, [](SamplerParams& p, float v) { p.amp.sustain = v / 100.0f; }, 0, 100, 1, 10, Step::Add, "%5.0f", {}},
    {"RELEASE MS", kC2, 8, [](const SamplerParams& p) { return p.amp.release_ms; }, [](SamplerParams& p, float v) { p.amp.release_ms = v; }, 1, 20000, 1.12f, 2, Step::Times, "%5.0f", {}},
    {"LOOP", kC2, 9, [](const SamplerParams& p) { return static_cast<float>(p.loop); }, [](SamplerParams& p, float v) { p.loop = static_cast<model::LoopMode>(v); }, 0, 2, 1, 1, Step::Add, nullptr, kLoop},

    {"FILTER", kC3, 5, [](const SamplerParams& p) { return static_cast<float>(p.filter); }, [](SamplerParams& p, float v) { p.filter = static_cast<model::FilterType>(v); }, 0, 3, 1, 1, Step::Add, nullptr, kFilter},
    {"CUTOFF HZ", kC3, 6, [](const SamplerParams& p) { return p.cutoff_hz; }, [](SamplerParams& p, float v) { p.cutoff_hz = v; }, 20, 20000, 1.0595f, 2, Step::Times, "%5.0f", {}},
    {"RESO %", kC3, 7, [](const SamplerParams& p) { return p.resonance * 100.0f; }, [](SamplerParams& p, float v) { p.resonance = v / 100.0f; }, 0, 100, 2, 10, Step::Add, "%5.0f", {}},
    {"ENV OCT", kC3, 8, [](const SamplerParams& p) { return p.env_amount; }, [](SamplerParams& p, float v) { p.env_amount = v; }, -8, 8, 0.25f, 1, Step::Add, "%5.2f", {}},
    {"KEYTRACK %", kC3, 9, [](const SamplerParams& p) { return p.key_track * 100.0f; }, [](SamplerParams& p, float v) { p.key_track = v / 100.0f; }, 0, 100, 5, 25, Step::Add, "%5.0f", {}},
    {"VEL>CUT OC", kC3, 10, [](const SamplerParams& p) { return p.vel_to_cutoff; }, [](SamplerParams& p, float v) { p.vel_to_cutoff = v; }, 0, 4, 0.25f, 1, Step::Add, "%5.2f", {}},

    {"F.ATTACK", kC3, 12, [](const SamplerParams& p) { return p.filter_env.attack_ms; }, [](SamplerParams& p, float v) { p.filter_env.attack_ms = v; }, 0.5f, 10000, 1.12f, 2, Step::Times, "%5.0f", {}},
    {"F.DECAY", kC3, 13, [](const SamplerParams& p) { return p.filter_env.decay_ms; }, [](SamplerParams& p, float v) { p.filter_env.decay_ms = v; }, 1, 20000, 1.12f, 2, Step::Times, "%5.0f", {}},
    {"F.SUSTAIN", kC3, 14, [](const SamplerParams& p) { return p.filter_env.sustain * 100.0f; }, [](SamplerParams& p, float v) { p.filter_env.sustain = v / 100.0f; }, 0, 100, 1, 10, Step::Add, "%5.0f", {}},
    {"F.RELEASE", kC3, 15, [](const SamplerParams& p) { return p.filter_env.release_ms; }, [](SamplerParams& p, float v) { p.filter_env.release_ms = v; }, 1, 20000, 1.12f, 2, Step::Times, "%5.0f", {}},

    // Loop points in seconds; the setter converts with the sample rate stored by the page.
    {"LOOP START", kC2, 12, nullptr, nullptr, 0, 0, 0.01f, 0.1f, Step::Add, "%6.3f", {}},
    {"LOOP END", kC2, 13, nullptr, nullptr, 0, 0, 0.01f, 0.1f, Step::Add, "%6.3f", {}},
    {"LOOP LEN", kC2, 14, nullptr, nullptr, 0, 0, 0, 0, Step::Add, "%6.3f", {}},
    {"INSTRUMENT", kC1, 12, nullptr, nullptr, 1, 16, 1, 1, Step::Add, "%5.0f", {}},
}};
// clang-format on

constexpr int kLoopStart = 21;
constexpr int kLoopEnd = 22;
constexpr int kLoopLen = 23;
constexpr int kInstField = 24;
constexpr int kValueOffset = 13;

class ControlPage final : public Page {
public:
    std::string_view id() const override { return "7"; }
    std::string_view title() const override { return "CONTROL PARAMETERS"; }
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
            if (focus_ == 0) {
                ui.set_focus(Focus::Command);
            } else {
                focus_ = neighbour(-1);
            }
            return true;
        case Key::Down:
        case Key::Tab:
            focus_ = neighbour(1);
            return true;
        case Key::Left:
            change(ui, -1, big);
            return true;
        case Key::Right:
        case Key::Enter:
            change(ui, 1, big);
            return true;
        case Key::PageUp:
        case Key::PageDown:
            change(ui, ev.key == Key::PageUp ? 1 : -1, true);
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (zone < 0 || zone >= static_cast<int>(kFields.size()) || zone == kLoopLen) {
            return;
        }
        focus_ = zone;
        if (wheel != 0) {
            change(ui, wheel, false);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const bool fields = ui.focus() == Focus::Fields;
        const model::Document& doc = ui.app().workspace().doc();
        const model::Instrument& inst = doc.current_instrument();
        const SamplerParams& p = inst.sampler;

        // Header line: instrument, sample.
        char buf[96];
        std::snprintf(buf, sizeof buf, "INSTRUMENT %02d  %s %s", doc.current + 1, fit(inst.name, 12).c_str(),
                      model::kind_name(inst.kind));
        widgets::text(c, ui, 1, 2, buf);
        if (inst.kind != model::InstrumentKind::Sampler) {
            widgets::small_text(c, ui, px(38), py(2) + 3, "ROOT, INTERP, LOOP: SAMPLER ONLY", gfx::kInkDim);
        } else if (inst.sample) {
            std::snprintf(buf, sizeof buf, "%.3f S  %s  ROOT %s", inst.sample->seconds(),
                          inst.sample->channels == 2 ? "STEREO" : "MONO", note_name(p.root_note).c_str());
            widgets::small_text(c, ui, px(38), py(2) + 3, buf, gfx::kInkDim);
        } else {
            widgets::small_text(c, ui, px(38), py(2) + 3, "EMPTY: IMPORT A SAMPLE ON PAGE 2", gfx::kInkWarn);
        }
        widgets::frame(c, ui, gfx::Rect{2, py(3) + 6, kCanvasW - 4, py(17) - 3 - (py(3) + 6)});
        widgets::small_text(c, ui, px(kC1), py(4) + 2, "SOUND", gfx::kInkDim);
        widgets::small_text(c, ui, px(kC2), py(4) + 2, "AMP ENVELOPE", gfx::kInkDim);
        widgets::small_text(c, ui, px(kC3), py(4) + 2, "FILTER", gfx::kInkDim);
        widgets::small_text(c, ui, px(kC2), py(11) + 2, "LOOP POINTS (S)", gfx::kInkDim);
        widgets::small_text(c, ui, px(kC3), py(11) + 2, "FILTER ENVELOPE", gfx::kInkDim);

        for (int i = 0; i < static_cast<int>(kFields.size()); ++i) {
            const Field& f = kFields[static_cast<std::size_t>(i)];
            widgets::label(c, ui, f.col, f.row, f.label, 11);
            widgets::value_field(c, ui, f.col + kValueOffset, f.row, 6, value_text(i, doc), fields && focus_ == i,
                                 i == kLoopLen ? -1 : i);
        }
        draw_envelope(c, ui, gfx::Rect{px(2), py(18) + 2, px(29), py(27) - py(18) - 4}, p.amp, "AMP ENVELOPE");
        draw_envelope(c, ui, gfx::Rect{px(33), py(18) + 2, px(29), py(27) - py(18) - 4}, p.filter_env, "FILTER ENVELOPE");
        widgets::small_text(c, ui, px(2), py(27) + 3,
                            "ARROWS SELECT, LEFT/RIGHT CHANGE (SHIFT = x10), ESC = COMMAND LINE.  UNDO: MOD+Z", gfx::kInkDim);
    }

private:
    // Up/down in screen order: next field below in the same column, else wrap through the list.
    int neighbour(int dir) const {
        const int n = static_cast<int>(kFields.size());
        int f = focus_;
        do {
            f = (f + dir + n) % n;
        } while (f == kLoopLen);
        return f;
    }

    static std::string value_text(int i, const model::Document& doc) {
        const model::Instrument& inst = doc.current_instrument();
        const SamplerParams& p = inst.sampler;
        const Field& f = kFields[static_cast<std::size_t>(i)];
        char buf[24];
        const double rate = inst.sample ? inst.sample->sample_rate : 48000.0;
        switch (i) {
        case kLoopStart:
            std::snprintf(buf, sizeof buf, f.format, static_cast<double>(p.loop_start) / rate);
            return buf;
        case kLoopEnd:
            std::snprintf(buf, sizeof buf, f.format, static_cast<double>(p.loop_end) / rate);
            return buf;
        case kLoopLen:
            std::snprintf(buf, sizeof buf, f.format,
                          static_cast<double>(p.loop_end > p.loop_start ? p.loop_end - p.loop_start : 0) / rate);
            return buf;
        case kInstField:
            std::snprintf(buf, sizeof buf, "%5d", doc.current + 1);
            return buf;
        default:
            break;
        }
        const float v = f.get(p);
        if (!f.labels.empty()) {
            return std::string(f.labels[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0,
                                                                                static_cast<int>(f.labels.size()) - 1))]);
        }
        if (f.mode == Step::Note) {
            return note_name(static_cast<int>(v)) + " " + std::to_string(static_cast<int>(v));
        }
        std::snprintf(buf, sizeof buf, f.format, static_cast<double>(v));
        return buf;
    }

    void change(Ui& ui, int dir, bool big) {
        model::Workspace& ws = ui.app().workspace();
        const Field& f = kFields[static_cast<std::size_t>(focus_)];
        if (focus_ == kInstField) {
            ws.select(std::clamp(ws.doc().current + dir * (big ? 4 : 1), 0, model::kInstruments - 1));
            return;
        }
        model::Instrument inst = ws.doc().current_instrument();
        SamplerParams& p = inst.sampler;
        if (focus_ == kLoopStart || focus_ == kLoopEnd) {
            if (!inst.sample) {
                ui.message("NO SAMPLE IN THIS INSTRUMENT", true);
                return;
            }
            const auto frames = static_cast<long>(inst.sample->frames);
            const auto delta = static_cast<long>(std::lround((big ? f.big : f.step) * static_cast<float>(inst.sample->sample_rate))) * dir;
            if (focus_ == kLoopStart) {
                p.loop_start = static_cast<std::size_t>(std::clamp(static_cast<long>(p.loop_start) + delta, 0L,
                                                                   static_cast<long>(p.loop_end) - 2));
            } else {
                p.loop_end = static_cast<std::size_t>(
                    std::clamp(static_cast<long>(p.loop_end) + delta, static_cast<long>(p.loop_start) + 2, frames));
            }
        } else {
            float v = f.get(p);
            if (!f.labels.empty()) {
                const int n = static_cast<int>(f.labels.size());
                v = static_cast<float>((static_cast<int>(v) + dir + n) % n);
            } else if (f.mode == Step::Times) {
                v = dir > 0 ? v * (big ? f.big : f.step) : v / (big ? f.big : f.step);
            } else {
                v += static_cast<float>(dir) * (big ? f.big : f.step);
            }
            f.set(p, std::clamp(v, f.lo, f.hi));
        }
        ws.set_instrument(ws.doc().current, inst, std::string(f.label), ui.app().time_s());
    }

    // ADSR shape: attack, decay, a fixed sustain segment, release, scaled by total time.
    static void draw_envelope(gfx::Canvas& c, const Ui& ui, gfx::Rect r, const model::Adsr& a, std::string_view name) {
        c.rect(r, gfx::kInkGrid);
        widgets::small_text(c, ui, r.x + 4, r.y + 3, name, gfx::kInkDim);
        const float hold = std::max(100.0f, (a.attack_ms + a.decay_ms + a.release_ms) * 0.3f);
        const float total = a.attack_ms + a.decay_ms + hold + a.release_ms;
        const auto x_of = [&](float ms) { return r.x + 3 + static_cast<int>(ms / total * static_cast<float>(r.w - 6)); };
        const auto y_of = [&](float level) { return r.y + r.h - 3 - static_cast<int>(level * static_cast<float>(r.h - 14)); };
        const int x0 = x_of(0);
        const int x1 = x_of(a.attack_ms);
        const int x2 = x_of(a.attack_ms + a.decay_ms);
        const int x3 = x_of(a.attack_ms + a.decay_ms + hold);
        const int x4 = x_of(total);
        c.line(x0, y_of(0), x1, y_of(1), gfx::kInkBright);
        c.line(x1, y_of(1), x2, y_of(a.sustain), gfx::kInkBright);
        c.line(x2, y_of(a.sustain), x3, y_of(a.sustain), gfx::kInkBright);
        c.line(x3, y_of(a.sustain), x4, y_of(0), gfx::kInkBright);
        c.dotted_line(x3, r.y + 2, x3, r.y + r.h - 3, 2, gfx::kInkGrid);
    }

    int focus_ = 0;
};

} // namespace

std::unique_ptr<Page> make_control_page() { return std::make_unique<ControlPage>(); }

} // namespace phos::ui
