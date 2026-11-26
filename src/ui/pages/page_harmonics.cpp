// Page 4 — HARMONIC PROFILES: the amplitude of one harmonic across all segments (top, drawn with the
// light pen), and the 32-harmonic spectrum of the cursor segment (bottom, also drawable). COMPUTE
// turns the harmonics into waveforms (PLAN.md §7.3).

#include <array>
#include <cmath>
#include <cstdio>

#include "ui/pages/pages.h"
#include "ui/pages/wave_common.h"

namespace phos::ui {

namespace {

enum Field : int { kHarmonic, kSegment, kCount, kNormalize, kCompute, kFieldCount };
constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};
constexpr gfx::Rect kProfile{8, py(4), kCanvasW - 16, py(15) - py(4)};
constexpr gfx::Rect kSpectrum{8, py(17), kCanvasW - 16, py(25) - py(17)};

class HarmonicsPage final : public Page {
public:
    std::string_view id() const override { return "4"; }
    std::string_view title() const override { return "HARMONIC PROFILES"; }
    bool has_fields() const override { return true; }

    void update(const InputState& in, Ui& ui) override {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst || !in.pen_inside || !in.pen_down) {
            return;
        }
        const int x = static_cast<int>(in.pen_x);
        const int y = static_cast<int>(in.pen_y);
        const auto& data = *inst->wavetable;
        const int n = static_cast<int>(data.segments.size());
        if (kProfile.contains(x, y)) {
            // Profile: x = segment, y = amplitude of the selected harmonic.
            const int seg = std::clamp((x - kProfile.x) * n / kProfile.w, 0, n - 1);
            const float v = 1.0f - static_cast<float>(y - kProfile.y) / static_cast<float>(kProfile.h - 1);
            segment_ = seg;
            wave::commit(ui, model::wavetable::set_harmonic(data, seg, harmonic_, v), "HARMONIC PROFILE");
        } else if (kSpectrum.contains(x, y)) {
            // Spectrum: x = harmonic, y = amplitude, in the cursor segment.
            const int h = std::clamp((x - kSpectrum.x) * 32 / kSpectrum.w, 0, 31);
            const float v = 1.0f - static_cast<float>(y - kSpectrum.y) / static_cast<float>(kSpectrum.h - 1);
            harmonic_ = h;
            wave::commit(ui, model::wavetable::set_harmonic(data, clamp_segment(*inst), h, v), "SPECTRUM");
        }
    }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type == InputEvent::Type::Text) {
            return ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_'
                       ? (change(ui, (ev.ch == '+' || ev.ch == '=') ? 1 : -1, false), true)
                       : false;
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
        case Key::Right:
            change(ui, ev.key == Key::Right ? 1 : -1, big);
            return true;
        case Key::Enter:
            if (focus_ == kCompute) {
                ui.execute(normalize_ ? "COMPUTE" : "COMPUTE RAW");
            } else {
                change(ui, 1, big);
            }
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        focus_ = std::clamp(zone / 16, 0, kFieldCount - 1);
        if (wheel != 0) {
            change(ui, wheel, false);
        } else if (focus_ == kCompute) {
            ui.execute(normalize_ ? "COMPUTE" : "COMPUTE RAW");
        } else if (focus_ == kNormalize) {
            normalize_ = zone % 16 == 1;
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            widgets::centered_text(c, ui, 0, kCols, 2, "HARMONIC PROFILES");
            wave::draw_not_wave(c, ui);
            return;
        }
        const bool fields = ui.focus() == Focus::Fields;
        auto focused = [&](int f) { return fields && focus_ == f; };
        const auto& segs = inst->wavetable->segments;
        const int n = static_cast<int>(segs.size());
        const int seg = clamp_segment(*inst);
        char buf[32];
        widgets::text(c, ui, 1, 2, "HARMONIC", gfx::kInkDim);
        std::snprintf(buf, sizeof buf, "%2d", harmonic_ + 1);
        widgets::value_field(c, ui, 10, 2, 3, buf, focused(kHarmonic), kHarmonic * 16);
        widgets::text(c, ui, 14, 2, "SEG", gfx::kInkDim);
        std::snprintf(buf, sizeof buf, "%3d", seg + 1);
        widgets::value_field(c, ui, 18, 2, 4, buf, focused(kSegment), kSegment * 16);
        widgets::text(c, ui, 23, 2, "OF", gfx::kInkDim);
        std::snprintf(buf, sizeof buf, "%3d", n);
        widgets::value_field(c, ui, 26, 2, 4, buf, focused(kCount), kCount * 16);
        widgets::text(c, ui, 31, 2, "NORM", gfx::kInkDim);
        widgets::option_group(c, ui, 36, 2, kOffOn, normalize_ ? 1 : 0, focused(kNormalize), kNormalize * 16);
        widgets::button(c, ui, 45, 2, "COMPUTE", focused(kCompute), kCompute * 16);

        // Profile of the selected harmonic across segments.
        c.rect(gfx::Rect{kProfile.x - 1, kProfile.y - 1, kProfile.w + 2, kProfile.h + 2}, gfx::kInkText);
        const int bar_w = std::max(1, kProfile.w / n);
        for (int s = 0; s < n; ++s) {
            const float a = segs[static_cast<std::size_t>(s)].harmonics[static_cast<std::size_t>(harmonic_)];
            const int h = static_cast<int>(a * static_cast<float>(kProfile.h - 2));
            const int x = kProfile.x + s * kProfile.w / n;
            if (h > 0) {
                c.fill_rect(gfx::Rect{x, kProfile.y + kProfile.h - 1 - h, std::max(1, bar_w - 1), h},
                            s == seg ? gfx::kInkBright : gfx::kInkText);
            }
        }
        std::snprintf(buf, sizeof buf, "HARMONIC %d OVER %d SEGMENTS", harmonic_ + 1, n);
        widgets::small_text(c, ui, kProfile.x + 3, kProfile.y + 2, buf, gfx::kInkDim);
        if (inst->wave.loop_length > 0) {
            const int x0 = kProfile.x + inst->wave.loop_start * kProfile.w / n;
            const int x1 = kProfile.x + (inst->wave.loop_start + inst->wave.loop_length) * kProfile.w / n - 1;
            c.hline(x0, x1, kProfile.y + kProfile.h + 2, gfx::kInkAccent);
        }

        // Spectrum of the cursor segment.
        c.rect(gfx::Rect{kSpectrum.x - 1, kSpectrum.y - 1, kSpectrum.w + 2, kSpectrum.h + 2}, gfx::kInkText);
        for (int h = 0; h < 32; ++h) {
            const float a = segs[static_cast<std::size_t>(seg)].harmonics[static_cast<std::size_t>(h)];
            const int bh = static_cast<int>(a * static_cast<float>(kSpectrum.h - 2));
            const int x = kSpectrum.x + h * kSpectrum.w / 32;
            if (bh > 0) {
                c.fill_rect(gfx::Rect{x + 2, kSpectrum.y + kSpectrum.h - 1 - bh, kSpectrum.w / 32 - 4, bh},
                            h == harmonic_ ? gfx::kInkBright : gfx::kInkDim);
            }
        }
        std::snprintf(buf, sizeof buf, "SEGMENT %d: HARMONICS 1-32", seg + 1);
        widgets::small_text(c, ui, kSpectrum.x + 3, kSpectrum.y + 2, buf, gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(26) + 2,
                            "DRAW THE PROFILE (TOP) OR SPECTRUM (BOTTOM) WITH THE PEN, THEN COMPUTE.",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(27) + 2, "RESULT ON PAGE 6, TABLE ON PAGE D. COMPUTE REPLACES DRAWN WAVES.",
                            gfx::kInkDim);
    }

private:
    int clamp_segment(const model::Instrument& inst) const {
        segment_ = std::clamp(segment_, 0, static_cast<int>(inst.wavetable->segments.size()) - 1);
        return segment_;
    }

    void change(Ui& ui, int dir, bool big) {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            return;
        }
        const int n = static_cast<int>(inst->wavetable->segments.size());
        switch (focus_) {
        case kHarmonic:
            harmonic_ = std::clamp(harmonic_ + dir * (big ? 8 : 1), 0, 31);
            break;
        case kSegment:
            segment_ = std::clamp(segment_ + dir * (big ? 8 : 1), 0, n - 1);
            break;
        case kCount:
            wave::commit(ui, model::wavetable::resize(*inst->wavetable, n + dir * (big ? 16 : 1)), "SEGMENTS");
            break;
        case kNormalize:
            normalize_ = !normalize_;
            break;
        default:
            break;
        }
    }

    int focus_ = kHarmonic;
    int harmonic_ = 0;
    mutable int segment_ = 0;
    bool normalize_ = true;
};

} // namespace

std::unique_ptr<Page> make_harmonics_page() { return std::make_unique<HarmonicsPage>(); }

} // namespace phos::ui
