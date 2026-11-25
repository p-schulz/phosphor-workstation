#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/text.h"
#include "gfx/palette.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

// Field indices, in focus order.
enum Field : int {
    kMonitor,
    kFullscreen,
    kAutoApply,
    kScale,
    kPanel,
    kOverscanL,
    kOverscanR,
    kOverscanT,
    kOverscanB,
    kTheme,
    kCrt,
    kScanlines,
    kMask,
    kBloom,
    kPersist,
    kVignette,
    kCurve,
    kBright,
    kFieldCount
};

// Zone ids: field * 16 + option index.
constexpr int zone_of(int field, int option = 0) { return field * 16 + option; }

constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};
constexpr std::array<std::string_view, 3> kScales = {"FIT", "INTEGER", "STRETCH"};
constexpr std::array<std::string_view, 1> kNext = {"NEXT"};

constexpr int kLabelCol = 2;
constexpr int kEqCols = 11; // '=' column offset from the label
constexpr int kValueCol = kLabelCol + kEqCols + 2;
constexpr int kRightLabelCol = 34;
constexpr int kRightValueCol = kRightLabelCol + kEqCols + 2;

bool is_number(int f) { return (f >= kOverscanL && f <= kOverscanB) || (f >= kScanlines && f <= kBright); }

float* number_ref(DisplayProfile& p, int f) {
    switch (f) {
    case kOverscanL:
        return &p.overscan.left;
    case kOverscanR:
        return &p.overscan.right;
    case kOverscanT:
        return &p.overscan.top;
    case kOverscanB:
        return &p.overscan.bottom;
    case kScanlines:
        return &p.crt.scanlines;
    case kMask:
        return &p.crt.mask;
    case kBloom:
        return &p.crt.bloom;
    case kPersist:
        return &p.crt.persistence;
    case kVignette:
        return &p.crt.vignette;
    case kCurve:
        return &p.crt.curvature;
    case kBright:
        return &p.crt.brightness;
    default:
        return nullptr;
    }
}

// Step, min, max for numeric fields (stored as fractions; overscan steps 0.5 %).
struct Range {
    float step, lo, hi;
};
Range number_range(int f) {
    if (f >= kOverscanL && f <= kOverscanB) {
        return {0.005f, 0.0f, 0.15f};
    }
    if (f == kBright) {
        return {0.01f, -0.5f, 0.5f};
    }
    return {0.01f, 0.0f, 1.0f};
}

std::string number_text(int f, float v) {
    char buf[16];
    if (f >= kOverscanL && f <= kOverscanB) {
        std::snprintf(buf, sizeof buf, "%4.1f", static_cast<double>(v) * 100.0);
    } else {
        std::snprintf(buf, sizeof buf, "%4d", static_cast<int>(std::lround(static_cast<double>(v) * 100.0)));
    }
    return buf;
}

int theme_index(const DisplayProfile& p) {
    const auto themes = gfx::all_themes();
    for (std::size_t i = 0; i < themes.size(); ++i) {
        if (iequals(themes[i].name, p.theme)) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

int panel_index(const DisplayProfile& p) {
    const auto choices = panel_aspect_choices();
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (std::abs(choices[i].value - p.panel_aspect) < 1e-6) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

int option_count(int f) {
    switch (f) {
    case kFullscreen:
    case kAutoApply:
    case kCrt:
        return 2;
    case kScale:
        return 3;
    case kPanel:
        return static_cast<int>(panel_aspect_choices().size());
    case kTheme:
        return static_cast<int>(gfx::all_themes().size());
    default:
        return 0;
    }
}

int option_value(const DisplayProfile& p, int f) {
    switch (f) {
    case kFullscreen:
        return p.fullscreen ? 1 : 0;
    case kAutoApply:
        return p.auto_apply ? 1 : 0;
    case kCrt:
        return p.crt.enabled ? 1 : 0;
    case kScale:
        return static_cast<int>(p.scale);
    case kPanel:
        return panel_index(p);
    case kTheme:
        return theme_index(p);
    default:
        return 0;
    }
}

void set_option(DisplayProfile& p, int f, int v) {
    const int n = option_count(f);
    if (n == 0) {
        return;
    }
    v = ((v % n) + n) % n;
    switch (f) {
    case kFullscreen:
        p.fullscreen = v == 1;
        break;
    case kAutoApply:
        p.auto_apply = v == 1;
        break;
    case kCrt:
        p.crt.enabled = v == 1;
        break;
    case kScale:
        p.scale = static_cast<gfx::ScaleMode>(v);
        break;
    case kPanel:
        p.panel_aspect = panel_aspect_choices()[static_cast<std::size_t>(v)].value;
        break;
    case kTheme:
        p.theme = std::string(gfx::all_themes()[static_cast<std::size_t>(v)].name);
        break;
    default:
        break;
    }
}

class SetupPage final : public Page {
public:
    std::string_view id() const override { return "S"; }
    std::string_view title() const override { return "SETUP"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type == InputEvent::Type::Text) {
            if (ev.ch == '+' || ev.ch == '=') {
                change(ui, +1);
                return true;
            }
            if (ev.ch == '-' || ev.ch == '_') {
                change(ui, -1);
                return true;
            }
            return false;
        }
        const int big = (ev.mods & kModShift) ? 10 : 1;
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
            change(ui, -big);
            return true;
        case Key::Right:
            change(ui, big);
            return true;
        case Key::PageDown:
            change(ui, -10);
            return true;
        case Key::PageUp:
            change(ui, 10);
            return true;
        case Key::Enter:
            if (focus_ == kMonitor) {
                ui.execute("DISPLAY NEXT");
            } else {
                change(ui, 1);
            }
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
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
        if (field == kMonitor) {
            ui.execute("DISPLAY NEXT");
        } else if (option_count(field) > 0) {
            DisplayProfile p = ui.app().profile();
            set_option(p, field, option);
            ui.app().set_profile(p);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const DisplayProfile& p = ui.app().profile();
        const DisplayStatus& d = ui.app().display();
        const bool fields = ui.focus() == Focus::Fields;
        auto focused = [&](int f) { return fields && focus_ == f; };

        widgets::frame(c, ui, gfx::Rect{2, py(2) + 6, kCanvasW - 4, py(21) - 3 - (py(2) + 6)}, "DISPLAY");

        widgets::label(c, ui, kLabelCol, 3, "MONITOR", kEqCols);
        widgets::text(c, ui, kValueCol, 3, fit(to_upper(d.monitor_name), 30));
        char buf[96];
        std::snprintf(buf, sizeof buf, "%d/%d", d.monitor_count > 0 ? d.monitor_index + 1 : 0, d.monitor_count);
        widgets::text(c, ui, 47, 3, buf, gfx::kInkDim);
        widgets::option_group(c, ui, 56, 3, kNext, focused(kMonitor) ? 0 : -1, false, zone_of(kMonitor));

        std::snprintf(buf, sizeof buf, "SIGNAL %dX%d %dHZ  FB %dX%d  PIXEL ASPECT %.3f", d.mode_w, d.mode_h,
                      d.refresh_hz, d.fb_w, d.fb_h, d.pixel_aspect);
        widgets::small_text(c, ui, px(kValueCol), py(4) + 2, buf, gfx::kInkDim);

        draw_options(c, ui, kFullscreen, kLabelCol, kValueCol, 5, "FULLSCREEN", kOffOn, p, fields);
        draw_options(c, ui, kAutoApply, kRightLabelCol, kRightValueCol, 5, "AUTO APPLY", kOffOn, p, fields);
        draw_options(c, ui, kScale, kLabelCol, kValueCol, 6, "SCALE", kScales, p, fields);
        std::array<std::string_view, 5> panels{};
        for (std::size_t i = 0; i < panels.size() && i < panel_aspect_choices().size(); ++i) {
            panels[i] = panel_aspect_choices()[i].label;
        }
        draw_options(c, ui, kPanel, kLabelCol, kValueCol, 7, "PANEL", panels, p, fields);

        widgets::label(c, ui, kLabelCol, 8, "OVERSCAN %", kEqCols);
        constexpr std::array<std::string_view, 4> kEdges = {"L", "R", "T", "B"};
        for (int i = 0; i < 4; ++i) {
            const int f = kOverscanL + i;
            const int col = kValueCol + i * 9;
            widgets::text(c, ui, col, 8, kEdges[static_cast<std::size_t>(i)]);
            DisplayProfile copy = p;
            widgets::value_field(c, ui, col + 2, 8, 5, number_text(f, *number_ref(copy, f)), focused(f), zone_of(f));
        }

        std::array<std::string_view, 5> themes{};
        for (std::size_t i = 0; i < themes.size() && i < gfx::all_themes().size(); ++i) {
            themes[i] = gfx::all_themes()[i].name;
        }
        draw_options(c, ui, kTheme, kLabelCol, kValueCol, 9, "THEME", themes, p, fields);

        c.dotted_line(px(1), py(10) + 6, kCanvasW - px(1), py(10) + 6, 3, gfx::kInkGrid);

        draw_options(c, ui, kCrt, kLabelCol, kValueCol, 11, "CRT PASS", kOffOn, p, fields);
        draw_number(c, ui, kScanlines, kLabelCol, kValueCol, 12, "SCANLINES", p, fields);
        draw_number(c, ui, kMask, kLabelCol, kValueCol, 13, "MASK", p, fields);
        draw_number(c, ui, kBloom, kLabelCol, kValueCol, 14, "BLOOM", p, fields);
        draw_number(c, ui, kPersist, kRightLabelCol, kRightValueCol, 12, "PERSIST", p, fields);
        draw_number(c, ui, kVignette, kRightLabelCol, kRightValueCol, 13, "VIGNETTE", p, fields);
        draw_number(c, ui, kCurve, kRightLabelCol, kRightValueCol, 14, "CURVATURE", p, fields);
        draw_number(c, ui, kBright, kRightLabelCol, kRightValueCol, 15, "BRIGHTNESS", p, fields);

        widgets::small_text(c, ui, px(kLabelCol), py(17) + 2,
                            "ARROWS SELECT, LEFT/RIGHT OR +/- CHANGE (SHIFT X10), ESC = COMMAND LINE", gfx::kInkDim);
        widgets::small_text(c, ui, px(kLabelCol), py(18) + 2,
                            "SAVED PER MONITOR IN DISPLAY_PROFILES.JSON.  TYPE TESTCARD TO CHECK GEOMETRY.",
                            gfx::kInkDim);

        widgets::frame(c, ui, gfx::Rect{2, py(21) + 6, kCanvasW - 4, py(29) - 3 - (py(21) + 6)}, "AUDIO / MIDI",
                       gfx::kInkGrid);
        constexpr std::array<std::pair<std::string_view, std::string_view>, 5> kLater = {{
            {"AUDIO DEVICE", "M1"},
            {"BUFFER / RATE", "M1"},
            {"MIDI INPUTS", "M2"},
            {"LATENCY REPORT", "M2"},
            {"MIDI OUTPUT", "M8"},
        }};
        for (std::size_t i = 0; i < kLater.size(); ++i) {
            const int row = 22 + static_cast<int>(i);
            widgets::text(c, ui, kLabelCol, row, kLater[i].first, gfx::kInkDim);
            widgets::text(c, ui, kValueCol + 2, row, "-- ARRIVES IN " + std::string(kLater[i].second), gfx::kInkGrid);
        }
    }

private:
    template <std::size_t N>
    void draw_options(gfx::Canvas& c, const Ui& ui, int f, int label_col, int value_col, int row, std::string_view name,
                      const std::array<std::string_view, N>& options, const DisplayProfile& p, bool fields) const {
        widgets::label(c, ui, label_col, row, name, kEqCols);
        const int n = std::min(static_cast<int>(N), option_count(f));
        widgets::option_group(c, ui, value_col, row, std::span<const std::string_view>(options.data(), static_cast<std::size_t>(n)),
                              option_value(p, f), fields && focus_ == f, zone_of(f));
    }

    void draw_number(gfx::Canvas& c, const Ui& ui, int f, int label_col, int value_col, int row, std::string_view name,
                     const DisplayProfile& p, bool fields) const {
        widgets::label(c, ui, label_col, row, name, kEqCols);
        DisplayProfile copy = p;
        widgets::value_field(c, ui, value_col, row, 5, number_text(f, *number_ref(copy, f)), fields && focus_ == f,
                             zone_of(f));
    }

    void change(Ui& ui, int delta) {
        if (focus_ == kMonitor) {
            ui.execute("DISPLAY NEXT");
            return;
        }
        DisplayProfile p = ui.app().profile();
        if (is_number(focus_)) {
            const Range r = number_range(focus_);
            float* v = number_ref(p, focus_);
            *v = std::clamp(*v + r.step * static_cast<float>(delta), r.lo, r.hi);
            // Snap to the step grid so repeated changes do not accumulate float error.
            *v = std::round(*v / r.step) * r.step;
        } else {
            set_option(p, focus_, option_value(p, focus_) + (delta > 0 ? 1 : -1));
        }
        ui.app().set_profile(p);
    }

    int focus_ = kFullscreen;
};

} // namespace

std::unique_ptr<Page> make_setup_page() { return std::make_unique<SetupPage>(); }

} // namespace phos::ui
