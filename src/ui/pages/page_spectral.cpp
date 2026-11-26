// Page 0 — SPECTRAL DRAWING: paint the harmonic matrix (time across, harmonics up) as an image with
// ADD / REMOVE / INVERT brushes; the right side shows it as a 3D surface. CREATE runs COMPUTE
// (docs/screens.md §3). Same data as Page 4, seen whole.

#include <array>
#include <cmath>
#include <cstdio>

#include "ui/pages/pages.h"
#include "ui/pages/wave_common.h"

namespace phos::ui {

namespace {

enum Field : int { kMode, kBrush, kClear, kSmooth, kCreate, kFieldCount };
enum Mode : int { kAdd, kRemove, kInvert };
constexpr std::array<std::string_view, 3> kModes = {"ADD", "REMOVE", "INVERT"};
constexpr std::array<std::string_view, 3> kBrushes = {"1", "2", "3"};
constexpr gfx::Rect kMatrix{8, py(4), 232, 32 * 6};      // 32 harmonic rows of 6 px
constexpr gfx::Rect kSurface{252, py(4), 252, 32 * 6};

class SpectralPage final : public Page {
public:
    std::string_view id() const override { return "0"; }
    std::string_view title() const override { return "SPECTRAL DRAWING"; }
    bool has_fields() const override { return true; }

    void update(const InputState& in, Ui& ui) override {
        const model::Instrument* inst = wave::instrument(ui);
        const int x = static_cast<int>(in.pen_x);
        const int y = static_cast<int>(in.pen_y);
        if (!inst || !in.pen_inside || !in.pen_down || !kMatrix.contains(x, y)) {
            if (!in.pen_down) {
                last_cell_ = -1;
            }
            return;
        }
        const int n = static_cast<int>(inst->wavetable->segments.size());
        const int seg = std::clamp((x - kMatrix.x) * n / kMatrix.w, 0, n - 1);
        const int harm = std::clamp(31 - (y - kMatrix.y) / 6, 0, 31); // harmonic 1 at the bottom
        const int cell = seg * 32 + harm;
        if (cell == last_cell_) {
            return; // INVERT must not flip the same cell every frame
        }
        last_cell_ = cell;
        model::WavetableData d = *inst->wavetable;
        const int r = brush_; // 0..2: radius in cells
        for (int ds = -r; ds <= r; ++ds) {
            for (int dh = -r; dh <= r; ++dh) {
                const int s = seg + ds;
                const int h = harm + dh;
                if (s < 0 || s >= n || h < 0 || h >= 32) {
                    continue;
                }
                const float falloff = 1.0f - 0.35f * static_cast<float>(std::max(std::abs(ds), std::abs(dh)));
                float& a = d.segments[static_cast<std::size_t>(s)].harmonics[static_cast<std::size_t>(h)];
                switch (mode_) {
                case kAdd:
                    a = std::min(1.0f, a + 0.35f * falloff);
                    break;
                case kRemove:
                    a = std::max(0.0f, a - 0.35f * falloff);
                    break;
                case kInvert:
                    a = 1.0f - a;
                    break;
                default:
                    break;
                }
            }
        }
        wave::commit(ui, std::move(d), "SPECTRAL PAINT");
    }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type != InputEvent::Type::Key) {
            return false;
        }
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
            if (focus_ == kMode) {
                mode_ = (mode_ + (ev.key == Key::Right ? 1 : 2)) % 3;
            } else if (focus_ == kBrush) {
                brush_ = (brush_ + (ev.key == Key::Right ? 1 : 2)) % 3;
            }
            return true;
        case Key::Enter:
            act(ui);
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int, Ui& ui) override {
        focus_ = std::clamp(zone / 16, 0, kFieldCount - 1);
        if (focus_ == kMode) {
            mode_ = zone % 16;
        } else if (focus_ == kBrush) {
            brush_ = zone % 16;
        } else {
            act(ui);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            widgets::centered_text(c, ui, 0, kCols, 2, "SPECTRAL DRAWING");
            wave::draw_not_wave(c, ui);
            return;
        }
        const bool fields = ui.focus() == Focus::Fields;
        widgets::option_group(c, ui, 1, 2, kModes, mode_, fields && focus_ == kMode, kMode * 16);
        widgets::text(c, ui, 21, 2, "BRUSH", gfx::kInkDim);
        widgets::option_group(c, ui, 27, 2, kBrushes, brush_, fields && focus_ == kBrush, kBrush * 16);
        widgets::button(c, ui, 34, 2, "CLEAR", fields && focus_ == kClear, kClear * 16);
        widgets::button(c, ui, 42, 2, "SMOOTH", fields && focus_ == kSmooth, kSmooth * 16);
        widgets::button(c, ui, 51, 2, "CREATE", fields && focus_ == kCreate, kCreate * 16);

        const auto& segs = inst->wavetable->segments;
        const int n = static_cast<int>(segs.size());
        // Matrix: one cell per segment x harmonic, shaded along the depth ramp.
        c.rect(gfx::Rect{kMatrix.x - 1, kMatrix.y - 1, kMatrix.w + 2, kMatrix.h + 2}, gfx::kInkText);
        for (int s = 0; s < n; ++s) {
            const int x0 = kMatrix.x + s * kMatrix.w / n;
            const int x1 = kMatrix.x + (s + 1) * kMatrix.w / n;
            for (int h = 0; h < 32; ++h) {
                const float a = segs[static_cast<std::size_t>(s)].harmonics[static_cast<std::size_t>(h)];
                if (a < 0.02f) {
                    continue;
                }
                const auto ink = static_cast<std::uint8_t>(gfx::kInkRamp1 + std::clamp(static_cast<int>(a * 6.0f), 0, 5));
                c.fill_rect(gfx::Rect{x0, kMatrix.y + (31 - h) * 6, std::max(1, x1 - x0), 5}, ink);
            }
        }
        widgets::small_text(c, ui, kMatrix.x, kMatrix.y + kMatrix.h + 4, "TIME (SEGMENTS) ->   HARMONIC 1 AT THE BOTTOM",
                            gfx::kInkDim);

        // Surface: each harmonic row as a polyline across time, oblique.
        wave::waterfall(c, kSurface, 32, std::min(n, 128), 70.0 / 31.0, 120.0 / 31.0, 60.0, [&](int r, int i) {
            const int s = n <= 1 ? 0 : i * (n - 1) / (std::min(n, 128) - 1);
            return segs[static_cast<std::size_t>(s)].harmonics[static_cast<std::size_t>(r)];
        });
        widgets::small_text(c, ui, px(1), py(26) + 2,
                            "PAINT WITH THE LIGHT PEN. CREATE = COMPUTE: HARMONICS BECOME WAVEFORMS.",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(27) + 2, "SAME DATA AS PAGE 4. UNDO: MOD+Z.", gfx::kInkDim);
    }

private:
    void act(Ui& ui) {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            return;
        }
        if (focus_ == kCreate) {
            ui.execute("COMPUTE");
            return;
        }
        model::WavetableData d = *inst->wavetable;
        if (focus_ == kClear) {
            for (auto& s : d.segments) {
                s.harmonics.fill(0.0f);
            }
            wave::commit(ui, std::move(d), "CLEAR HARMONICS", false);
        } else if (focus_ == kSmooth) {
            // Blur along time (3-tap) for every harmonic.
            const auto src = d;
            const int n = static_cast<int>(d.segments.size());
            for (int s = 0; s < n; ++s) {
                for (std::size_t h = 0; h < 32; ++h) {
                    const float a = src.segments[static_cast<std::size_t>(std::max(0, s - 1))].harmonics[h];
                    const float b = src.segments[static_cast<std::size_t>(s)].harmonics[h];
                    const float cc = src.segments[static_cast<std::size_t>(std::min(n - 1, s + 1))].harmonics[h];
                    d.segments[static_cast<std::size_t>(s)].harmonics[h] = (a + 2.0f * b + cc) / 4.0f;
                }
            }
            wave::commit(ui, std::move(d), "SMOOTH HARMONICS", false);
        }
    }

    int focus_ = kMode;
    int mode_ = kAdd;
    int brush_ = 0;
    int last_cell_ = -1;
};

} // namespace

std::unique_ptr<Page> make_spectral_page() { return std::make_unique<SpectralPage>(); }

} // namespace phos::ui
