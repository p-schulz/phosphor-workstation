// Page D — WAVEFORM DISPLAY: 3D waterfall of the wavetable segments (docs/screens.md §3):
// option bar FORMAT A/B/C, END SEG 32/64/128, STEP 1/2/4/8 above the drawing.

#include <array>
#include <cstdio>

#include "ui/pages/pages.h"
#include "ui/pages/wave_common.h"

namespace phos::ui {

namespace {

enum Field : int { kFormat, kEnd, kStep, kFieldCount };
constexpr std::array<std::string_view, 3> kFormats = {"A", "B", "C"};
constexpr std::array<std::string_view, 3> kEnds = {"32", "64", "128"};
constexpr std::array<std::string_view, 4> kSteps = {"1", "2", "4", "8"};

class WaveformPage final : public Page {
public:
    std::string_view id() const override { return "D"; }
    std::string_view title() const override { return "WAVEFORM DISPLAY"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type != InputEvent::Type::Key) {
            return false;
        }
        switch (ev.key) {
        case Key::Up:
            ui.set_focus(Focus::Command);
            return true;
        case Key::Tab:
        case Key::Down:
            focus_ = (focus_ + 1) % kFieldCount;
            return true;
        case Key::Left:
        case Key::Right:
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
        value_[static_cast<std::size_t>(focus_)] = zone % 16;
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        widgets::centered_text(c, ui, 0, kCols, 2, "WAVEFORM DISPLAY");
        c.rect(gfx::Rect{4, py(3) + 1, kCanvasW - 8, kCellH + 2}, gfx::kInkText);
        const bool fields = ui.focus() == Focus::Fields;
        widgets::text(c, ui, 1, 3, "DISPLAY  FORMAT:");
        widgets::option_group(c, ui, 18, 3, kFormats, value_[kFormat], fields && focus_ == kFormat, kFormat * 16);
        widgets::text(c, ui, 25, 3, "END SEG:");
        widgets::option_group(c, ui, 34, 3, kEnds, value_[kEnd], fields && focus_ == kEnd, kEnd * 16);
        widgets::text(c, ui, 46, 3, "STEP:");
        widgets::option_group(c, ui, 52, 3, kSteps, value_[kStep], fields && focus_ == kStep, kStep * 16);

        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            wave::draw_not_wave(c, ui);
            return;
        }
        const auto& segs = inst->wavetable->segments;
        const int end = std::min(static_cast<int>(segs.size()), 32 << value_[kEnd]);
        const int step = 1 << value_[kStep];
        const int rows = (end + step - 1) / step;
        const gfx::Rect area{12, py(5), kCanvasW - 24, py(26) - py(5)};
        // A: oblique, B: steep (more depth), C: flat overlay.
        const double depth = value_[kFormat] == 2 ? 0.0 : 1.0;
        const double dx = depth * (value_[kFormat] == 1 ? 90.0 : 200.0) / std::max(1, rows - 1);
        const double dy = depth * (value_[kFormat] == 1 ? 170.0 : 110.0) / std::max(1, rows - 1);
        const double amp = value_[kFormat] == 2 ? 100.0 : 45.0;
        wave::waterfall(c, area, rows, 128, dx, dy, amp, [&](int r, int i) {
            return segs[static_cast<std::size_t>(r * step)].wave[static_cast<std::size_t>(i)];
        });
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s  %d SEGMENTS  SHOWN %d", inst->name.c_str(), static_cast<int>(segs.size()), rows);
        widgets::small_text(c, ui, px(1), py(27) + 2, buf, gfx::kInkDim);
    }

private:
    void change(int dir) {
        const int n = focus_ == kFormat ? 3 : focus_ == kEnd ? 3 : 4;
        auto& v = value_[static_cast<std::size_t>(focus_)];
        v = (v + dir + n) % n;
    }

    int focus_ = kFormat;
    std::array<int, kFieldCount> value_ = {0, 2, 0};
};

} // namespace

std::unique_ptr<Page> make_waveform_page() { return std::make_unique<WaveformPage>(); }

} // namespace phos::ui
