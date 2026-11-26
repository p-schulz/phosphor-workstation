// Page 5 — SYNTH: oscillators, unison, LFO and glide of a SYNTH instrument; for a DRUM instrument the
// 16-pad table instead (PLAN.md §7.3). Filter and envelopes of every kind are on Page 7.

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

using model::SynthParams;

constexpr std::array<std::string_view, 6> kOsc = {"SAW", "PULSE", "TRI", "SINE", "NOISE", "OFF"};
constexpr std::array<std::string_view, 4> kLfo = {"SINE", "TRI", "SQR", "S&H"};
constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};

struct SynthField {
    std::string_view label;
    int col;
    int row;
    float (*get)(const SynthParams&);
    void (*set)(SynthParams&, float);
    float lo, hi, step;
    const char* format;
    std::span<const std::string_view> labels;
};

// clang-format off
const std::array<SynthField, 20> kSynth = {{
    {"OSC 1", 1, 4, [](const SynthParams& s) { return static_cast<float>(s.osc1); }, [](SynthParams& s, float v) { s.osc1 = static_cast<model::OscWave>(v); }, 0, 5, 1, nullptr, kOsc},
    {"OSC 1 LEVEL", 1, 5, [](const SynthParams& s) { return s.osc1_level * 100.0f; }, [](SynthParams& s, float v) { s.osc1_level = v / 100.0f; }, 0, 100, 5, "%5.0f", {}},
    {"OSC 2", 1, 6, [](const SynthParams& s) { return static_cast<float>(s.osc2); }, [](SynthParams& s, float v) { s.osc2 = static_cast<model::OscWave>(v); }, 0, 5, 1, nullptr, kOsc},
    {"OSC 2 LEVEL", 1, 7, [](const SynthParams& s) { return s.osc2_level * 100.0f; }, [](SynthParams& s, float v) { s.osc2_level = v / 100.0f; }, 0, 100, 5, "%5.0f", {}},
    {"OSC 2 SEMI", 1, 8, [](const SynthParams& s) { return static_cast<float>(s.osc2_semitones); }, [](SynthParams& s, float v) { s.osc2_semitones = static_cast<int>(v); }, -24, 24, 1, "%5.0f", {}},
    {"OSC 2 CENT", 1, 9, [](const SynthParams& s) { return s.osc2_detune; }, [](SynthParams& s, float v) { s.osc2_detune = v; }, -100, 100, 1, "%5.0f", {}},
    {"PULSE W %", 1, 10, [](const SynthParams& s) { return s.pulse_width * 100.0f; }, [](SynthParams& s, float v) { s.pulse_width = v / 100.0f; }, 5, 95, 1, "%5.0f", {}},
    {"SUB %", 1, 11, [](const SynthParams& s) { return s.sub_level * 100.0f; }, [](SynthParams& s, float v) { s.sub_level = v / 100.0f; }, 0, 100, 5, "%5.0f", {}},
    {"NOISE %", 1, 12, [](const SynthParams& s) { return s.noise_level * 100.0f; }, [](SynthParams& s, float v) { s.noise_level = v / 100.0f; }, 0, 100, 5, "%5.0f", {}},
    {"UNISON", 1, 14, [](const SynthParams& s) { return static_cast<float>(s.unison); }, [](SynthParams& s, float v) { s.unison = static_cast<int>(v); }, 1, 4, 1, "%5.0f", {}},
    {"SPREAD CT", 1, 15, [](const SynthParams& s) { return s.unison_spread; }, [](SynthParams& s, float v) { s.unison_spread = v; }, 0, 50, 1, "%5.0f", {}},
    {"LFO SHAPE", 33, 4, [](const SynthParams& s) { return static_cast<float>(s.lfo_shape); }, [](SynthParams& s, float v) { s.lfo_shape = static_cast<model::LfoShape>(v); }, 0, 3, 1, nullptr, kLfo},
    {"LFO RATE HZ", 33, 5, [](const SynthParams& s) { return s.lfo_rate_hz; }, [](SynthParams& s, float v) { s.lfo_rate_hz = v; }, 0.05f, 30, 0.1f, "%5.2f", {}},
    {"LFO SYNC", 33, 6, [](const SynthParams& s) { return static_cast<float>(s.lfo_sync); }, [](SynthParams& s, float v) { s.lfo_sync = static_cast<int>(v); }, 0, 64, 1, "%5.0f", {}},
    {"LFO>PITCH", 33, 7, [](const SynthParams& s) { return s.lfo_to_pitch; }, [](SynthParams& s, float v) { s.lfo_to_pitch = v; }, 0, 1200, 5, "%5.0f", {}},
    {"LFO>CUTOFF", 33, 8, [](const SynthParams& s) { return s.lfo_to_cutoff; }, [](SynthParams& s, float v) { s.lfo_to_cutoff = v; }, 0, 4, 0.1f, "%5.1f", {}},
    {"LFO>PW %", 33, 9, [](const SynthParams& s) { return s.lfo_to_pw * 100.0f; }, [](SynthParams& s, float v) { s.lfo_to_pw = v / 100.0f; }, 0, 45, 1, "%5.0f", {}},
    {"LFO>AMP %", 33, 10, [](const SynthParams& s) { return s.lfo_to_amp * 100.0f; }, [](SynthParams& s, float v) { s.lfo_to_amp = v / 100.0f; }, 0, 100, 5, "%5.0f", {}},
    {"GLIDE MS", 33, 12, [](const SynthParams& s) { return s.glide_ms; }, [](SynthParams& s, float v) { s.glide_ms = v; }, 0, 2000, 10, "%5.0f", {}},
    {"MONO", 33, 13, [](const SynthParams& s) { return s.mono ? 1.0f : 0.0f; }, [](SynthParams& s, float v) { s.mono = v > 0.5f; }, 0, 1, 1, nullptr, kOffOn},
}};
// clang-format on

// Drum pad columns.
enum PadCol : int { kType, kTune, kDecay, kTone, kLevel, kPan, kChoke, kSample, kPadCols };
constexpr std::array<std::string_view, kPadCols> kPadHeads = {"TYPE", "TUNE", "DECAY", "TONE", "LEVEL", "PAN", "CHOKE", "SAMPLE"};
constexpr std::array<int, kPadCols> kPadColX = {8, 16, 22, 29, 35, 42, 48, 55};
constexpr int kPadTop = 5;

class SynthPage final : public Page {
public:
    std::string_view id() const override { return "5"; }
    std::string_view title() const override { return "SYNTH"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type == InputEvent::Type::Text) {
            return ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_'
                       ? (change(ui, (ev.ch == '+' || ev.ch == '=') ? 1 : -1, false), true)
                       : false;
        }
        const bool big = (ev.mods & kModShift) != 0;
        const bool drum = kind(ui) == model::InstrumentKind::Drum;
        switch (ev.key) {
        case Key::Up:
            if ((drum ? pad_ : field_) == 0) {
                ui.set_focus(Focus::Command);
            } else if (drum) {
                --pad_;
            } else {
                --field_;
            }
            return true;
        case Key::Down:
            if (drum) {
                pad_ = std::min(pad_ + 1, model::kDrumPads - 1);
            } else {
                field_ = std::min(field_ + 1, static_cast<int>(kSynth.size()) - 1);
            }
            return true;
        case Key::Tab:
            if (drum) {
                col_ = (col_ + 1) % kPadCols;
            } else {
                field_ = (field_ + 1) % static_cast<int>(kSynth.size());
            }
            return true;
        case Key::Left:
        case Key::Right:
            change(ui, ev.key == Key::Right ? 1 : -1, big);
            return true;
        case Key::Enter:
            if (drum) { // audition the pad
                ui.app().keyjazz_note(model::kDrumBaseNote + pad_, true);
                ui.app().keyjazz_note(model::kDrumBaseNote + pad_, false);
            } else {
                change(ui, 1, big);
            }
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (kind(ui) == model::InstrumentKind::Drum) {
            pad_ = std::clamp(zone / 16, 0, model::kDrumPads - 1);
            col_ = std::clamp(zone % 16, 0, kPadCols - 1);
        } else {
            field_ = std::clamp(zone / 16, 0, static_cast<int>(kSynth.size()) - 1);
        }
        if (wheel != 0) {
            change(ui, wheel, false);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const model::Document& doc = ui.app().workspace().doc();
        const model::Instrument& inst = doc.current_instrument();
        char buf[96];
        std::snprintf(buf, sizeof buf, "INSTRUMENT %02d  %s  (%s)", doc.current + 1, fit(inst.name, 14).c_str(),
                      model::kind_name(inst.kind));
        widgets::text(c, ui, 1, 2, buf);
        if (inst.kind == model::InstrumentKind::Drum) {
            draw_drum(c, ui, inst);
            return;
        }
        if (inst.kind != model::InstrumentKind::Synth) {
            widgets::text(c, ui, 2, 5, "PAGE 5 EDITS SYNTH AND DRUM INSTRUMENTS.", gfx::kInkDim);
            widgets::text(c, ui, 2, 7, "TYPE KIND SYNTH OR KIND DRUM (UNDOABLE),", gfx::kInkDim);
            widgets::text(c, ui, 2, 8, "OR INST <N> TO PICK ANOTHER INSTRUMENT.", gfx::kInkDim);
            return;
        }
        const bool fields = ui.focus() == Focus::Fields;
        widgets::small_text(c, ui, px(1), py(3) + 3, "OSCILLATORS", gfx::kInkDim);
        widgets::small_text(c, ui, px(33), py(3) + 3, "LFO AND GLIDE", gfx::kInkDim);
        for (int i = 0; i < static_cast<int>(kSynth.size()); ++i) {
            const SynthField& f = kSynth[static_cast<std::size_t>(i)];
            widgets::label(c, ui, f.col, f.row, f.label, 12);
            const float v = f.get(inst.synth);
            std::string text;
            if (!f.labels.empty()) {
                text = std::string(f.labels[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0, static_cast<int>(f.labels.size()) - 1))]);
            } else {
                std::snprintf(buf, sizeof buf, f.format, static_cast<double>(v));
                text = buf;
            }
            if (i == 13 && inst.synth.lfo_sync > 0) { // LFO SYNC: show the note length
                std::snprintf(buf, sizeof buf, "%d/16", inst.synth.lfo_sync);
                text = buf;
            }
            widgets::value_field(c, ui, f.col + 14, f.row, 6, text, fields && field_ == i, i * 16);
        }
        widgets::small_text(c, ui, px(1), py(18) + 2, "FILTER, ENVELOPES, LEVEL AND VELOCITY: PAGE 7.  LFO SYNC 0 = FREE RATE,",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(19) + 2, "ELSE SIXTEENTHS PER CYCLE.  MONO = LEGATO: HELD NOTES GLIDE WITHOUT RETRIGGER.",
                            gfx::kInkDim);
    }

private:
    static model::InstrumentKind kind(const Ui& ui) { return ui.app().workspace().doc().current_instrument().kind; }

    void draw_drum(gfx::Canvas& c, const Ui& ui, const model::Instrument& inst) const {
        const bool fields = ui.focus() == Focus::Fields;
        widgets::text(c, ui, 1, 4, "PAD", gfx::kInkDim);
        for (int k = 0; k < kPadCols; ++k) {
            widgets::small_text(c, ui, px(kPadColX[static_cast<std::size_t>(k)]), py(4) + 3, kPadHeads[static_cast<std::size_t>(k)], gfx::kInkDim);
        }
        char buf[16];
        for (int p = 0; p < model::kDrumPads; ++p) {
            const model::DrumPad& pad = inst.drum[static_cast<std::size_t>(p)];
            const int row = kPadTop + p;
            widgets::text(c, ui, 1, row, note_name(model::kDrumBaseNote + p), pad.type == model::DrumType::Off ? gfx::kInkGrid : gfx::kInkText);
            for (int k = 0; k < kPadCols; ++k) {
                std::string v;
                switch (k) {
                case kType:
                    v = model::drum_type_name(pad.type);
                    break;
                case kTune:
                    std::snprintf(buf, sizeof buf, "%+3.0f", static_cast<double>(pad.tune));
                    v = buf;
                    break;
                case kDecay:
                    std::snprintf(buf, sizeof buf, "%4.0f", static_cast<double>(pad.decay_ms));
                    v = buf;
                    break;
                case kTone:
                    std::snprintf(buf, sizeof buf, "%3.0f", static_cast<double>(pad.tone * 100.0f));
                    v = buf;
                    break;
                case kLevel:
                    std::snprintf(buf, sizeof buf, "%4.0f", static_cast<double>(pad.level_db));
                    v = buf;
                    break;
                case kPan:
                    std::snprintf(buf, sizeof buf, "%+3.0f", static_cast<double>(pad.pan * 100.0f));
                    v = buf;
                    break;
                case kChoke:
                    v = pad.choke == 0 ? "--" : std::to_string(pad.choke);
                    break;
                case kSample:
                    v = pad.sample_slot < 0 ? "--" : std::to_string(pad.sample_slot + 1);
                    break;
                default:
                    break;
                }
                const int width = k == kType ? 6 : 5;
                widgets::value_field(c, ui, kPadColX[static_cast<std::size_t>(k)], row, width, v,
                                     fields && pad_ == p && col_ == k, p * 16 + k);
            }
        }
        widgets::small_text(c, ui, px(1), py(22) + 2,
                            "UP/DOWN PAD, TAB COLUMN, LEFT/RIGHT CHANGE, ENTER PLAYS. PADS = NOTES C2-D#3.",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(23) + 2,
                            "CHOKE: ONE GROUP CUTS ITSELF (CLOSED/OPEN HAT). SAMPLE PADS PLAY INSTRUMENT N.",
                            gfx::kInkDim);
    }

    void change(Ui& ui, int dir, bool big) {
        model::Workspace& ws = ui.app().workspace();
        model::Instrument inst = ws.doc().current_instrument();
        if (inst.kind == model::InstrumentKind::Drum) {
            model::DrumPad& p = inst.drum[static_cast<std::size_t>(pad_)];
            const float d = static_cast<float>(dir);
            switch (col_) {
            case kType:
                p.type = static_cast<model::DrumType>((static_cast<int>(p.type) + dir + 8) % 8);
                break;
            case kTune:
                p.tune = std::clamp(p.tune + d * (big ? 12.0f : 1.0f), -36.0f, 36.0f);
                break;
            case kDecay:
                p.decay_ms = std::clamp(dir > 0 ? p.decay_ms * (big ? 2.0f : 1.12f) : p.decay_ms / (big ? 2.0f : 1.12f), 5.0f, 5000.0f);
                break;
            case kTone:
                p.tone = std::clamp(p.tone + d * (big ? 0.25f : 0.05f), 0.0f, 1.0f);
                break;
            case kLevel:
                p.level_db = std::clamp(p.level_db + d * (big ? 6.0f : 1.0f), -40.0f, 6.0f);
                break;
            case kPan:
                p.pan = std::clamp(p.pan + d * (big ? 0.5f : 0.1f), -1.0f, 1.0f);
                break;
            case kChoke:
                p.choke = (p.choke + dir + 5) % 5;
                break;
            case kSample:
                p.sample_slot = std::clamp(p.sample_slot + dir, -1, model::kInstruments - 1);
                break;
            default:
                break;
            }
            ws.set_instrument(ws.doc().current, inst, "DRUM PAD", ui.app().time_s());
            return;
        }
        if (inst.kind != model::InstrumentKind::Synth) {
            return;
        }
        const SynthField& f = kSynth[static_cast<std::size_t>(field_)];
        float v = f.get(inst.synth);
        if (!f.labels.empty()) {
            const int n = static_cast<int>(f.labels.size());
            v = static_cast<float>((static_cast<int>(v) + dir + n) % n);
        } else {
            v += static_cast<float>(dir) * f.step * (big ? 10.0f : 1.0f);
        }
        f.set(inst.synth, std::clamp(v, f.lo, f.hi));
        ws.set_instrument(ws.doc().current, inst, std::string(f.label), ui.app().time_s());
    }

    int field_ = 0;
    int pad_ = 0;
    int col_ = kType;
};

} // namespace

std::unique_ptr<Page> make_synth_page() { return std::make_unique<SynthPage>(); }

} // namespace phos::ui
