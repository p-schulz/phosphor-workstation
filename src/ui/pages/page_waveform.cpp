// Page D — WAVEFORM DISPLAY: 3D waterfall of the current sound as 128 slices (docs/screens.md §3).
// Option bar FORMAT A/B/C, END SEG 32/64/128, STEP 1/2/4/8; for samples a second bar SAMPLE
// CYCLE/SPAN and RANGE ALL/LOOP. A wavetable is filled in between its segments the way it plays
// (real segments brighter); a sample is cut into 128 equal time slices (model/wave_slices.h).

#include <array>
#include <cmath>
#include <cstdio>

#include "model/wave_slices.h"
#include "ui/pages/pages.h"
#include "ui/pages/wave_common.h"
#include "ui/pages/waterfall.h"

namespace phos::ui {

namespace {

enum Field : int { kFormat, kEnd, kStep, kView, kRange, kFieldCount };
constexpr std::array<std::string_view, 3> kFormats = {"A", "B", "C"};
constexpr std::array<std::string_view, 3> kEnds = {"32", "64", "128"};
constexpr std::array<std::string_view, 4> kSteps = {"1", "2", "4", "8"};
constexpr std::array<std::string_view, 2> kViews = {"CYCLE", "SPAN"};
constexpr std::array<std::string_view, 2> kRanges = {"ALL", "LOOP"};
constexpr std::array<int, kFieldCount> kOptionCount = {3, 3, 4, 2, 2};

std::string note_name(int note) {
    static constexpr std::array<const char*, 12> kNames = {"C", "C#", "D", "D#", "E", "F",
                                                           "F#", "G", "G#", "A", "A#", "B"};
    return std::string(kNames[static_cast<std::size_t>((note % 12 + 12) % 12)]) + std::to_string(note / 12 - 1);
}

// What the page shows; the slices are rebuilt only when this changes.
// Holds the data it was built from, so a freed and reused address cannot look unchanged.
struct SourceKey {
    std::shared_ptr<const void> table;
    model::WavetableParams wave;
    std::shared_ptr<const void> sample;
    std::size_t start = 0;
    std::size_t end = 0;
    double period = 0.0;
    int view = 0;
    friend bool operator==(const SourceKey&, const SourceKey&) = default;
};

class WaveformPage final : public Page {
public:
    std::string_view id() const override { return "D"; }
    std::string_view title() const override { return "WAVEFORM DISPLAY"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type != InputEvent::Type::Key) {
            return false;
        }
        const int fields = has_sample(ui) ? kFieldCount : kView;
        switch (ev.key) {
        case Key::Up:
            ui.set_focus(Focus::Command);
            return true;
        case Key::Tab:
        case Key::Down:
            focus_ = (std::min(focus_, fields - 1) + 1) % fields;
            return true;
        case Key::Left:
        case Key::Right:
            focus_ = std::min(focus_, fields - 1);
            change(ev.key == Key::Right ? 1 : -1);
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui&) override {
        focus_ = std::clamp(zone / 16, 0, kFieldCount - 1);
        if (wheel != 0) {
            change(wheel);
            return;
        }
        value_[static_cast<std::size_t>(focus_)] = std::min(zone % 16, kOptionCount[static_cast<std::size_t>(focus_)] - 1);
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        widgets::centered_text(c, ui, 0, kCols, 2, "WAVEFORM DISPLAY");
        c.rect(gfx::Rect{4, py(3) + 1, kCanvasW - 8, kCellH + 2}, gfx::kInkText);
        const bool fields = ui.focus() == Focus::Fields;
        auto focused = [&](int f) { return fields && focus_ == f; };
        widgets::text(c, ui, 1, 3, "DISPLAY  FORMAT:");
        widgets::option_group(c, ui, 18, 3, kFormats, value_[kFormat], focused(kFormat), kFormat * 16);
        widgets::text(c, ui, 25, 3, "END SEG:");
        widgets::option_group(c, ui, 34, 3, kEnds, value_[kEnd], focused(kEnd), kEnd * 16);
        widgets::text(c, ui, 46, 3, "STEP:");
        widgets::option_group(c, ui, 52, 3, kSteps, value_[kStep], focused(kStep), kStep * 16);

        const model::Instrument& inst = ui.app().workspace().doc().current_instrument();
        const bool table = inst.kind == model::InstrumentKind::Wavetable && inst.wavetable;
        const bool sample = !table && sample_of(inst) != nullptr;
        if (!table && !sample) {
            draw_nothing(c, ui);
            return;
        }
        char info[96];
        if (table) {
            update(SourceKey{inst.wavetable, inst.wave, nullptr}, [&] {
                return model::slices::from_wavetable(*inst.wavetable, inst.wave);
            });
            std::snprintf(info, sizeof info, "%s  %d SEGMENTS AS 128 SLICES", inst.name.c_str(),
                          static_cast<int>(inst.wavetable->segments.size()));
        } else {
            widgets::text(c, ui, 1, 4, "SAMPLE:");
            widgets::option_group(c, ui, 9, 4, kViews, value_[kView], focused(kView), kView * 16);
            widgets::text(c, ui, 25, 4, "RANGE:");
            widgets::option_group(c, ui, 34, 4, kRanges, value_[kRange], focused(kRange), kRange * 16);
            const model::SampleData& s = *sample_of(inst);
            const model::SamplerParams& p = inst.sampler;
            const bool loop = value_[kRange] == 1 && p.loop != model::LoopMode::Off && p.loop_end > p.loop_start;
            const std::size_t start = loop ? p.loop_start : 0;
            const std::size_t end = loop ? std::min(p.loop_end, s.frames) : s.frames;
            const int root = inst.kind == model::InstrumentKind::Snes ? inst.snes.root_note : p.root_note;
            const double hz = 440.0 * std::pow(2.0, (root - 69 + p.fine_cents / 100.0) / 12.0);
            const double period = s.sample_rate / hz;
            const auto view = value_[kView] == 0 ? model::slices::SampleView::Cycle : model::slices::SampleView::Span;
            update(SourceKey{nullptr, {}, inst.sample, start, end, period, value_[kView]},
                   [&] { return model::slices::from_sample(s, start, end, period, view); });
            const double secs = s.sample_rate ? static_cast<double>(end - start) / s.sample_rate : 0.0;
            if (view == model::slices::SampleView::Cycle) {
                std::snprintf(info, sizeof info, "%s  %.2f S%s  CYCLE %.1f FRAMES AT ROOT %s", inst.name.c_str(), secs,
                              loop ? " LOOP" : "", period, note_name(root).c_str());
            } else {
                std::snprintf(info, sizeof info, "%s  %.2f S%s  128 EQUAL SLICES", inst.name.c_str(), secs,
                              loop ? " LOOP" : "");
            }
        }

        const int end = 32 << value_[kEnd];
        const int step = 1 << value_[kStep];
        const int rows = (end + step - 1) / step;
        const gfx::Rect area{12, py(5), kCanvasW - 24, py(26) - py(5)};
        // A: oblique, B: steep (more depth), C: flat overlay (no hidden lines).
        wave::WaterfallView v;
        const int gaps = std::max(1, rows - 1);
        switch (value_[kFormat]) {
        case 0: v = {150.0 / gaps, 140.0 / gaps, 50.0, true}; break;
        case 1: v = {60.0 / gaps, 175.0 / gaps, 36.0, true}; break;
        default: v = {0.0, 0.0, 110.0, false}; break;
        }
        const auto& sl = slices_;
        wave::waterfall(
            c, area, rows, model::slices::kPoints, v,
            [&](int r, int i) { return sl.rows[static_cast<std::size_t>(r * step)][static_cast<std::size_t>(i)]; },
            [&](int r) -> std::uint8_t {
                if (sl.keyframe[static_cast<std::size_t>(r * step)] && rows > 1) {
                    return gfx::kInkBright;
                }
                // Depth ramp, front bright; the brightest step stays for the real segments.
                const int ramp = rows > 1 ? 4 - r * 4 / (rows - 1) : 4;
                return static_cast<std::uint8_t>(gfx::kInkRamp1 + ramp);
            });
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s  SHOWN %d", info, rows);
        widgets::small_text(c, ui, px(1), py(27) + 2, buf, gfx::kInkDim);
    }

private:
    static const model::SampleData* sample_of(const model::Instrument& inst) {
        const bool kind = inst.kind == model::InstrumentKind::Sampler || inst.kind == model::InstrumentKind::Snes;
        return kind && inst.sample && inst.sample->frames > 1 ? inst.sample.get() : nullptr;
    }
    static bool has_sample(const Ui& ui) {
        const model::Instrument& inst = ui.app().workspace().doc().current_instrument();
        return inst.kind != model::InstrumentKind::Wavetable && sample_of(inst) != nullptr;
    }

    template <class Build> void update(const SourceKey& key, Build build) const {
        if (!built_ || !(key == key_)) {
            slices_ = build();
            key_ = key;
            built_ = true;
        }
    }

    static void draw_nothing(gfx::Canvas& c, const Ui& ui) {
        const auto& doc = ui.app().workspace().doc();
        char buf[96];
        std::snprintf(buf, sizeof buf, "INSTRUMENT %02d IS A %s WITHOUT A WAVETABLE OR SAMPLE.", doc.current + 1,
                      model::kind_name(doc.current_instrument().kind));
        widgets::text(c, ui, 2, 6, buf, gfx::kInkDim);
        widgets::text(c, ui, 2, 8, "PAGE D SHOWS WAVETABLES AND SAMPLES (SAMPLER, SNES).", gfx::kInkDim);
        widgets::text(c, ui, 2, 9, "INST <N> PICKS ANOTHER INSTRUMENT.", gfx::kInkDim);
    }

    void change(int dir) {
        const int n = kOptionCount[static_cast<std::size_t>(focus_)];
        auto& v = value_[static_cast<std::size_t>(focus_)];
        v = (v + dir + n) % n;
    }

    int focus_ = kFormat;
    std::array<int, kFieldCount> value_ = {0, 2, 0, 0, 0};
    mutable model::slices::Slices slices_;
    mutable SourceKey key_;
    mutable bool built_ = false;
};

} // namespace

std::unique_ptr<Page> make_waveform_page() { return std::make_unique<WaveformPage>(); }

} // namespace phos::ui
