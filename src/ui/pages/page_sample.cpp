// Page 8 — SOUND SAMPLING: waveform of the current instrument's sample with selection and loop
// markers; trim, fades, normalize, reverse and loop crossfade as undoable edits (PLAN.md §7.2).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/text.h"
#include "dsp/smoother.h"
#include "model/sample_data.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

enum Field : int {
    kSelStart,
    kSelEnd,
    kLoopMode,
    kLoopStart,
    kLoopEnd,
    kXfade,
    kActions, // first action button
};

enum Action : int { kTrim, kFadeIn, kFadeOut, kNormalize, kReverse, kXfadeLoop, kSelAll, kSelLoop, kPlay, kUndo, kRedo };
constexpr std::array<std::string_view, 11> kActionNames = {"TRIM",   "FADE IN", "FADE OUT", "NORMALIZE",
                                                           "REVERSE", "XFADE LOOP", "SEL ALL", "SEL=LOOP",
                                                           "PLAY",   "UNDO",    "REDO"};
constexpr int kFieldCount = kActions + static_cast<int>(kActionNames.size());
constexpr std::array<std::string_view, 3> kLoopModes = {"OFF", "FWD", "PING"};

// Waveform area.
constexpr int kWaveX = 8;
constexpr int kWaveW = kCanvasW - 16;
constexpr int kWaveTop = py(4);
constexpr int kWaveH = py(14) - py(4);
constexpr int kWaveZones = 124; // 4 px each
constexpr int kZoneWave = 1000;

class SamplePage final : public Page {
public:
    std::string_view id() const override { return "8"; }
    std::string_view title() const override { return "SOUND SAMPLING"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        sync(ui);
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
                --focus_;
            }
            return true;
        case Key::Down:
        case Key::Tab:
            focus_ = std::min(focus_ + 1, kFieldCount - 1);
            return true;
        case Key::Left:
            change(ui, -1, big);
            return true;
        case Key::Right:
            change(ui, 1, big);
            return true;
        case Key::PageUp:
        case Key::PageDown:
            change(ui, ev.key == Key::PageUp ? 1 : -1, true);
            return true;
        case Key::Enter:
            if (focus_ >= kActions) {
                act(ui, focus_ - kActions);
            } else {
                change(ui, 1, big);
            }
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        sync(ui);
        const model::Instrument& inst = ui.app().workspace().doc().current_instrument();
        if (zone >= kZoneWave && zone < kZoneWave + kWaveZones) {
            if (!inst.sample) {
                return;
            }
            // Click on the waveform: move the focused marker there (selection start by default).
            const std::size_t frame = static_cast<std::size_t>(zone - kZoneWave) * inst.sample->frames / kWaveZones;
            const int target = (focus_ == kSelEnd || focus_ == kLoopStart || focus_ == kLoopEnd) ? focus_ : kSelStart;
            set_position(ui, target, frame);
            return;
        }
        if (zone < 0 || zone >= kFieldCount * 16) {
            return;
        }
        focus_ = zone / 16;
        if (wheel != 0) {
            change(ui, wheel, false);
        } else if (focus_ == kLoopMode) {
            set_loop_mode(ui, zone % 16);
        } else if (focus_ >= kActions) {
            act(ui, focus_ - kActions);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        sync(ui);
        const bool fields = ui.focus() == Focus::Fields;
        const model::Document& doc = ui.app().workspace().doc();
        const model::Instrument& inst = doc.current_instrument();
        char buf[128];
        if (!inst.sample) {
            std::snprintf(buf, sizeof buf, "INSTRUMENT %02d IS EMPTY: IMPORT A SAMPLE ON PAGE 2", doc.current + 1);
            widgets::text(c, ui, 1, 2, buf, gfx::kInkDim);
            c.rect(gfx::Rect{kWaveX, kWaveTop, kWaveW, kWaveH}, gfx::kInkGrid);
            return;
        }
        const model::SampleData& s = *inst.sample;
        std::snprintf(buf, sizeof buf, "%02d %s", doc.current + 1, fit(to_upper(s.name), 22).c_str());
        widgets::text(c, ui, 1, 2, buf);
        std::snprintf(buf, sizeof buf, "%.3f S  %u HZ  %s  PEAK %.1f DB  %.1f MB", s.seconds(), s.sample_rate,
                      s.channels == 2 ? "STEREO" : "MONO", static_cast<double>(dsp::gain_to_db(s.peak())),
                      static_cast<double>(s.bytes()) / (1024.0 * 1024.0));
        widgets::small_text(c, ui, px(27), py(2) + 3, buf, gfx::kInkDim);
        draw_wave(c, ui, inst);

        const double rate = s.sample_rate;
        auto secs = [&](std::size_t f) {
            std::snprintf(buf, sizeof buf, "%7.3f", static_cast<double>(f) / rate);
            return std::string(buf);
        };
        auto focused = [&](int f) { return fields && focus_ == f; };
        widgets::label(c, ui, 1, 15, "SEL START", 10);
        widgets::value_field(c, ui, 13, 15, 8, secs(sel_start_), focused(kSelStart), kSelStart * 16);
        widgets::label(c, ui, 23, 15, "SEL END", 8);
        widgets::value_field(c, ui, 33, 15, 8, secs(sel_end_), focused(kSelEnd), kSelEnd * 16);
        widgets::text(c, ui, 43, 15, "LEN" + secs(sel_end_ - sel_start_) + " S", gfx::kInkDim);
        const model::SamplerParams& p = inst.sampler;
        widgets::label(c, ui, 1, 16, "LOOP", 10);
        widgets::option_group(c, ui, 13, 16, kLoopModes, static_cast<int>(p.loop), focused(kLoopMode), kLoopMode * 16);
        widgets::label(c, ui, 1, 17, "LOOP FROM", 10);
        widgets::value_field(c, ui, 13, 17, 8, secs(p.loop_start), focused(kLoopStart), kLoopStart * 16);
        widgets::label(c, ui, 23, 17, "LOOP TO", 8);
        widgets::value_field(c, ui, 33, 17, 8, secs(p.loop_end), focused(kLoopEnd), kLoopEnd * 16);
        widgets::label(c, ui, 43, 17, "XFADE MS", 9);
        std::snprintf(buf, sizeof buf, "%4d", xfade_ms_);
        widgets::value_field(c, ui, 53, 17, 5, buf, focused(kXfade), kXfade * 16);

        // Action buttons: two rows.
        int col = 1;
        int row = 19;
        for (int a = 0; a < static_cast<int>(kActionNames.size()); ++a) {
            const auto name = kActionNames[static_cast<std::size_t>(a)];
            if (a == kSelAll) {
                col = 1;
                row = 21;
            }
            widgets::button(c, ui, col, row, name, focused(kActions + a), (kActions + a) * 16);
            col += static_cast<int>(name.size()) + 3;
        }
        const auto& ws = ui.app().workspace();
        std::snprintf(buf, sizeof buf, "UNDO STEPS %zu%s", ws.undo_depth(), ws.can_redo() ? "  (REDO AVAILABLE)" : "");
        widgets::small_text(c, ui, px(1), py(24) + 2, buf, gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(25) + 2,
                            "TRIM KEEPS THE SELECTION. FADES AND REVERSE WORK ON THE SELECTION, NORMALIZE ON ALL.",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(26) + 2,
                            "CLICK THE WAVEFORM TO PLACE THE FOCUSED MARKER. LEFT/RIGHT 10 MS, SHIFT 100 MS.",
                            gfx::kInkDim);
    }

private:
    // Resets the selection when the sample changes (import, undo, another instrument).
    void sync(const Ui& ui) const {
        const model::Instrument& inst = ui.app().workspace().doc().current_instrument();
        const model::SampleData* s = inst.sample.get();
        if (s != seen_) {
            seen_ = s;
            sel_start_ = 0;
            sel_end_ = s ? s->frames : 0;
        }
        if (s) {
            sel_end_ = std::min(sel_end_, s->frames);
            sel_start_ = std::min(sel_start_, sel_end_);
        }
    }

    void draw_wave(gfx::Canvas& c, const Ui& ui, const model::Instrument& inst) const {
        const model::SampleData& s = *inst.sample;
        const gfx::Rect box{kWaveX, kWaveTop, kWaveW, kWaveH};
        const int mid = box.y + box.h / 2;
        const auto x_of = [&](std::size_t frame) {
            return box.x + static_cast<int>(static_cast<double>(frame) / static_cast<double>(std::max<std::size_t>(1, s.frames)) * box.w);
        };
        // Selection band.
        c.fill_rect(gfx::Rect{x_of(sel_start_), box.y, std::max(1, x_of(sel_end_) - x_of(sel_start_)), box.h},
                    gfx::kInkField);
        c.hline(box.x, box.x + box.w - 1, mid, gfx::kInkGrid);
        const auto& o = s.overview;
        const float scale = static_cast<float>(box.h / 2 - 2);
        for (int x = 0; x < box.w; ++x) {
            const std::size_t b0 = static_cast<std::size_t>(x) * model::Overview::kBins / static_cast<std::size_t>(box.w);
            const std::size_t b1 = std::max(b0 + 1, static_cast<std::size_t>(x + 1) * model::Overview::kBins /
                                                        static_cast<std::size_t>(box.w));
            float lo = 0.0f;
            float hi = 0.0f;
            float rms = 0.0f;
            for (std::size_t b = b0; b < b1 && b < o.min.size(); ++b) {
                lo = std::min(lo, o.min[b]);
                hi = std::max(hi, o.max[b]);
                rms = std::max(rms, o.rms[b]);
            }
            const int y_hi = mid - static_cast<int>(std::lround(std::clamp(hi, -1.0f, 1.0f) * scale));
            const int y_lo = mid - static_cast<int>(std::lround(std::clamp(lo, -1.0f, 1.0f) * scale));
            c.vline(box.x + x, y_hi, y_lo, gfx::kInkText);
            const int r = static_cast<int>(std::lround(std::min(rms, 1.0f) * scale));
            if (r > 0) {
                c.vline(box.x + x, mid - r, mid + r, gfx::kInkBright);
            }
        }
        c.rect(gfx::Rect{box.x - 1, box.y - 1, box.w + 2, box.h + 2}, gfx::kInkText);
        // Loop markers.
        const model::SamplerParams& p = inst.sampler;
        if (p.loop != model::LoopMode::Off) {
            for (const std::size_t f : {p.loop_start, p.loop_end}) {
                const int x = std::min(x_of(f), box.x + box.w - 1);
                c.dotted_line(x, box.y, x, box.y + box.h - 1, 2, gfx::kInkAccent);
            }
            widgets::small_text(c, ui, x_of(p.loop_start) + 2, box.y + 2, "L", gfx::kInkAccent);
            widgets::small_text(c, ui, std::min(x_of(p.loop_end), box.x + box.w - 1) - 7, box.y + 2, "L", gfx::kInkAccent);
        }
        for (int z = 0; z < kWaveZones; ++z) {
            ui.add_zone(gfx::Rect{box.x + z * box.w / kWaveZones, box.y, box.w / kWaveZones + 1, box.h}, ZoneOwner::Page,
                        kZoneWave + z);
        }
    }

    void set_position(Ui& ui, int field, std::size_t frame) {
        model::Workspace& ws = ui.app().workspace();
        model::Instrument inst = ws.doc().current_instrument();
        if (!inst.sample) {
            return;
        }
        const std::size_t frames = inst.sample->frames;
        frame = std::min(frame, frames);
        switch (field) {
        case kSelStart:
            sel_start_ = std::min(frame, sel_end_);
            return;
        case kSelEnd:
            sel_end_ = std::max(frame, sel_start_);
            return;
        case kLoopStart:
            inst.sampler.loop_start = std::min(frame, inst.sampler.loop_end > 1 ? inst.sampler.loop_end - 2 : 0);
            ws.set_instrument(ws.doc().current, inst, "LOOP START", ui.app().time_s());
            return;
        case kLoopEnd:
            inst.sampler.loop_end = std::max(frame, inst.sampler.loop_start + 2);
            inst.sampler.loop_end = std::min(inst.sampler.loop_end, frames);
            ws.set_instrument(ws.doc().current, inst, "LOOP END", ui.app().time_s());
            return;
        default:
            return;
        }
    }

    void set_loop_mode(Ui& ui, int mode) {
        model::Workspace& ws = ui.app().workspace();
        model::Instrument inst = ws.doc().current_instrument();
        inst.sampler.loop = static_cast<model::LoopMode>(std::clamp(mode, 0, 2));
        if (inst.sample && inst.sampler.loop_end <= inst.sampler.loop_start) {
            inst.sampler.loop_start = 0;
            inst.sampler.loop_end = inst.sample->frames;
        }
        ws.set_instrument(ws.doc().current, inst, "LOOP MODE");
    }

    void change(Ui& ui, int dir, bool big) {
        const model::Instrument& inst = ui.app().workspace().doc().current_instrument();
        if (focus_ >= kActions || !inst.sample) {
            return;
        }
        if (focus_ == kLoopMode) {
            set_loop_mode(ui, (static_cast<int>(inst.sampler.loop) + dir + 3) % 3);
            return;
        }
        if (focus_ == kXfade) {
            xfade_ms_ = std::clamp(xfade_ms_ + dir * (big ? 50 : 5), 1, 2000);
            return;
        }
        const auto step = static_cast<long>(inst.sample->sample_rate * (big ? 0.1 : 0.01)) * dir;
        const std::size_t cur = focus_ == kSelStart   ? sel_start_
                                : focus_ == kSelEnd   ? sel_end_
                                : focus_ == kLoopStart ? inst.sampler.loop_start
                                                       : inst.sampler.loop_end;
        set_position(ui, focus_, static_cast<std::size_t>(std::max(0L, static_cast<long>(cur) + step)));
    }

    void act(Ui& ui, int action) {
        if (action == kUndo) {
            ui.execute("UNDO");
            return;
        }
        if (action == kRedo) {
            ui.execute("REDO");
            return;
        }
        model::Workspace& ws = ui.app().workspace();
        const int index = ws.doc().current;
        model::Instrument inst = ws.doc().current_instrument();
        if (!inst.sample) {
            ui.message("NO SAMPLE IN THIS INSTRUMENT", true);
            return;
        }
        const model::SampleData& s = *inst.sample;
        model::SamplerParams& p = inst.sampler;
        switch (action) {
        case kTrim:
            if (sel_end_ - sel_start_ < 2) {
                ui.message("TRIM: SELECT A RANGE FIRST", true);
                return;
            }
            inst.sample = model::edit::trim(s, sel_start_, sel_end_);
            // Keep the loop where it was in the material.
            p.loop_start = p.loop_start > sel_start_ ? std::min(p.loop_start - sel_start_, inst.sample->frames) : 0;
            p.loop_end = p.loop_end > sel_start_ ? std::min(p.loop_end - sel_start_, inst.sample->frames) : inst.sample->frames;
            if (p.loop_end <= p.loop_start + 1) {
                p.loop_start = 0;
                p.loop_end = inst.sample->frames;
            }
            break;
        case kFadeIn:
            inst.sample = model::edit::fade_in(s, sel_start_, sel_end_);
            break;
        case kFadeOut:
            inst.sample = model::edit::fade_out(s, sel_start_, sel_end_);
            break;
        case kNormalize:
            inst.sample = model::edit::normalize(s);
            break;
        case kReverse:
            inst.sample = model::edit::reverse(s, sel_start_, sel_end_);
            break;
        case kXfadeLoop: {
            if (p.loop == model::LoopMode::Off) {
                ui.message("XFADE LOOP: SWITCH THE LOOP ON FIRST", true);
                return;
            }
            const auto len = static_cast<std::size_t>(xfade_ms_ * 0.001 * s.sample_rate);
            if (p.loop_start < len) {
                ui.message("XFADE LOOP: LOOP START MUST BE AFTER THE XFADE LENGTH", true);
                return;
            }
            inst.sample = model::edit::crossfade_loop(s, p.loop_start, p.loop_end, len);
            break;
        }
        case kSelAll:
            sel_start_ = 0;
            sel_end_ = s.frames;
            return;
        case kSelLoop:
            sel_start_ = p.loop_start;
            sel_end_ = std::max(p.loop_end, p.loop_start);
            return;
        case kPlay:
            playing_ = !playing_;
            ui.app().audition_instrument(index, playing_);
            ui.message(playing_ ? "PLAYING (PLAY AGAIN TO STOP)" : "STOPPED");
            return;
        default:
            return;
        }
        const auto selection = std::make_pair(sel_start_, sel_end_);
        ws.set_instrument(index, inst, std::string(kActionNames[static_cast<std::size_t>(action)]));
        seen_ = inst.sample.get(); // keep the selection across the edit
        if (action == kTrim) {
            sel_start_ = 0;
            sel_end_ = inst.sample->frames;
        } else {
            std::tie(sel_start_, sel_end_) = selection;
        }
        ui.message(std::string(kActionNames[static_cast<std::size_t>(action)]) + " DONE (UNDO: MOD+Z)");
    }

    int focus_ = kSelStart;
    // Selection follows the sample; draw() resyncs it, hence mutable.
    mutable const model::SampleData* seen_ = nullptr;
    mutable std::size_t sel_start_ = 0;
    mutable std::size_t sel_end_ = 0;
    int xfade_ms_ = 20;
    bool playing_ = false;
};

} // namespace

std::unique_ptr<Page> make_sample_page() { return std::make_unique<SamplePage>(); }

} // namespace phos::ui
