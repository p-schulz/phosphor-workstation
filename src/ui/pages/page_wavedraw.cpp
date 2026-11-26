// Page 6 — WAVEFORM DRAWING: draw one wavetable segment with the light pen (freehand; LINE joins two
// clicks), SMOOTH, COPY to another segment, INTERPOLATE between two segments (PLAN.md §7.3).

#include <array>
#include <cmath>
#include <cstdio>

#include "ui/pages/pages.h"
#include "ui/pages/wave_common.h"

namespace phos::ui {

namespace {

enum Field : int { kSegment, kCount, kCycles, kLoopStart, kLoopLen, kXfade, kAuthentic, kTarget, kTool, kFieldCount };
enum Tool : int { kFree, kLine, kSmooth, kCopy, kInterp, kClear, kTools };
constexpr std::array<std::string_view, kTools> kToolNames = {"FREE", "LINE", "SMOOTH", "COPY TO", "INTERP FROM", "CLEAR"};
constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};

constexpr gfx::Rect kArea{8, py(5), kCanvasW - 16, py(20) - py(5)};
constexpr int kZoneTool = 2000;

class WavedrawPage final : public Page {
public:
    std::string_view id() const override { return "6"; }
    std::string_view title() const override { return "WAVEFORM DRAWING"; }
    bool has_fields() const override { return true; }

    void update(const InputState& in, Ui& ui) override {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst || !in.pen_inside || !kArea.contains(static_cast<int>(in.pen_x), static_cast<int>(in.pen_y))) {
            drawing_ = false;
            return;
        }
        const int point = std::clamp(static_cast<int>((in.pen_x - static_cast<float>(kArea.x)) * 128.0f / static_cast<float>(kArea.w)), 0, 127);
        const float value = std::clamp(1.0f - 2.0f * (in.pen_y - static_cast<float>(kArea.y)) / static_cast<float>(kArea.h), -1.0f, 1.0f);
        const int seg = clamp_segment(*inst);
        if (tool_ == kLine) {
            if (in.pen_pressed) {
                if (!line_anchor_) {
                    line_anchor_ = true;
                    anchor_point_ = point;
                    anchor_value_ = value;
                    ui.message("LINE: CLICK THE END POINT");
                } else {
                    line_anchor_ = false;
                    wave::commit(ui, model::wavetable::set_points(*inst->wavetable, seg, anchor_point_, point, anchor_value_, value),
                                 "LINE", false);
                }
            }
            return;
        }
        if (!in.pen_down) {
            drawing_ = false;
            return;
        }
        // Freehand: join to the previous pen position so fast strokes leave no gaps.
        const int from = drawing_ ? last_point_ : point;
        const float from_value = drawing_ ? last_value_ : value;
        wave::commit(ui, model::wavetable::set_points(*inst->wavetable, seg, from, point, from_value, value), "DRAW");
        drawing_ = true;
        last_point_ = point;
        last_value_ = value;
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
            if (focus_ == kTool) {
                apply_tool(ui);
            } else {
                change(ui, 1, big);
            }
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (zone >= kZoneTool) {
            focus_ = kTool;
            tool_ = zone - kZoneTool;
            apply_tool(ui);
            return;
        }
        focus_ = std::clamp(zone / 16, 0, kFieldCount - 1);
        if (wheel != 0) {
            change(ui, wheel, false);
        } else if (focus_ == kXfade || focus_ == kAuthentic) {
            change(ui, 1, false);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            widgets::centered_text(c, ui, 0, kCols, 2, "WAVEFORM DRAWING");
            wave::draw_not_wave(c, ui);
            return;
        }
        const bool fields = ui.focus() == Focus::Fields;
        auto focused = [&](int f) { return fields && focus_ == f; };
        const auto& segs = inst->wavetable->segments;
        const int seg = clamp_segment(*inst);
        char buf[32];
        auto field = [&](int f, int label_col, int col, int row, std::string_view label, int width, const std::string& v) {
            widgets::text(c, ui, label_col, row, label, gfx::kInkDim);
            widgets::value_field(c, ui, col, row, width, v, focused(f), f * 16);
        };
        std::snprintf(buf, sizeof buf, "%3d", seg + 1);
        field(kSegment, 1, 5, 2, "SEG", 4, buf);
        std::snprintf(buf, sizeof buf, "%3d", static_cast<int>(segs.size()));
        field(kCount, 10, 13, 2, "OF", 4, buf);
        std::snprintf(buf, sizeof buf, "%3d", segs[static_cast<std::size_t>(seg)].cycles);
        field(kCycles, 18, 25, 2, "CYCLES", 4, buf);
        std::snprintf(buf, sizeof buf, "%3d", inst->wave.loop_start + 1);
        field(kLoopStart, 30, 35, 2, "LOOP", 4, buf);
        std::snprintf(buf, sizeof buf, "%3d", inst->wave.loop_length);
        field(kLoopLen, 40, 44, 2, "LEN", 4, buf);
        widgets::text(c, ui, 1, 3, "XFADE", gfx::kInkDim);
        widgets::option_group(c, ui, 7, 3, kOffOn, inst->wave.crossfade ? 1 : 0, focused(kXfade), kXfade * 16);
        widgets::text(c, ui, 15, 3, "AUTHENTIC", gfx::kInkDim);
        widgets::option_group(c, ui, 25, 3, kOffOn, inst->wave.authentic ? 1 : 0, focused(kAuthentic), kAuthentic * 16);
        std::snprintf(buf, sizeof buf, "%3d", target_ + 1);
        field(kTarget, 33, 44, 3, "OTHER SEG", 4, buf);

        // Waveform: previous segment dimmed, current bright, zero line, grid.
        c.rect(gfx::Rect{kArea.x - 1, kArea.y - 1, kArea.w + 2, kArea.h + 2}, gfx::kInkText);
        const int mid = kArea.y + kArea.h / 2;
        c.hline(kArea.x, kArea.x + kArea.w - 1, mid, gfx::kInkGrid);
        for (int q = 1; q < 4; ++q) {
            c.dotted_line(kArea.x + kArea.w * q / 4, kArea.y, kArea.x + kArea.w * q / 4, kArea.y + kArea.h - 1, 3, gfx::kInkGrid);
        }
        auto plot = [&](const std::array<float, 128>& w, std::uint8_t ink) {
            std::array<gfx::Point, 128> pts{};
            for (int i = 0; i < 128; ++i) {
                pts[static_cast<std::size_t>(i)] = gfx::Point{kArea.x + i * (kArea.w - 1) / 127,
                                                              mid - static_cast<int>(w[static_cast<std::size_t>(i)] * static_cast<float>(kArea.h / 2 - 2))};
            }
            c.polyline(pts, ink);
        };
        if (seg > 0) {
            plot(segs[static_cast<std::size_t>(seg - 1)].wave, gfx::kInkGrid);
        }
        plot(segs[static_cast<std::size_t>(seg)].wave, gfx::kInkBright);
        if (line_anchor_) {
            c.vline(kArea.x + anchor_point_ * (kArea.w - 1) / 127, kArea.y, kArea.y + kArea.h - 1, gfx::kInkAccent);
        }

        int col = 1;
        for (int t = 0; t < kTools; ++t) {
            const auto name = kToolNames[static_cast<std::size_t>(t)];
            widgets::button(c, ui, col, 21, name, focused(kTool) && tool_ == t, kZoneTool + t);
            if (t == tool_ && t <= kLine) {
                c.hline(px(col), px(col) + static_cast<int>(name.size()) * kCellW + 8, py(22) + 2, gfx::kInkBright);
            }
            col += static_cast<int>(name.size()) + 3;
        }
        widgets::small_text(c, ui, px(1), py(24) + 2, "DRAW WITH THE LIGHT PEN (FREE) OR CLICK TWO POINTS (LINE). COPY TO / INTERP",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(25) + 2, "FROM USE 'OTHER SEG'. SEGMENTS ADVANCE AFTER THEIR CYCLES; THE LOOP REPEATS",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(1), py(26) + 2, "WHILE THE KEY IS HELD. AUTHENTIC = NO ANTI-ALIASING. UNDO: MOD+Z.", gfx::kInkDim);
    }

private:
    int clamp_segment(const model::Instrument& inst) const {
        segment_ = std::clamp(segment_, 0, static_cast<int>(inst.wavetable->segments.size()) - 1);
        target_ = std::clamp(target_, 0, static_cast<int>(inst.wavetable->segments.size()) - 1);
        return segment_;
    }

    void apply_tool(Ui& ui) {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            return;
        }
        const int seg = clamp_segment(*inst);
        const auto& data = *inst->wavetable;
        switch (tool_) {
        case kFree:
        case kLine:
            line_anchor_ = false;
            ui.message(tool_ == kFree ? "FREEHAND: DRAG ACROSS THE WAVEFORM" : "LINE: CLICK THE START POINT");
            return;
        case kSmooth:
            wave::commit(ui, model::wavetable::smooth(data, seg), "SMOOTH", false);
            return;
        case kCopy:
            wave::commit(ui, model::wavetable::copy_segment(data, seg, target_), "COPY SEGMENT", false);
            ui.message("COPIED SEGMENT " + std::to_string(seg + 1) + " TO " + std::to_string(target_ + 1));
            return;
        case kInterp:
            wave::commit(ui, model::wavetable::interpolate(data, target_, seg), "INTERPOLATE", false);
            ui.message("INTERPOLATED SEGMENTS " + std::to_string(std::min(seg, target_) + 1) + "-" +
                       std::to_string(std::max(seg, target_) + 1));
            return;
        case kClear:
            wave::commit(ui, model::wavetable::set_points(data, seg, 0, 127, 0.0f, 0.0f), "CLEAR SEGMENT", false);
            return;
        default:
            return;
        }
    }

    void change(Ui& ui, int dir, bool big) {
        const model::Instrument* inst = wave::instrument(ui);
        if (!inst) {
            return;
        }
        const int n = static_cast<int>(inst->wavetable->segments.size());
        model::WavetableParams w = inst->wave;
        switch (focus_) {
        case kSegment:
            segment_ = std::clamp(segment_ + dir * (big ? 8 : 1), 0, n - 1);
            return;
        case kTarget:
            target_ = std::clamp(target_ + dir * (big ? 8 : 1), 0, n - 1);
            return;
        case kCount:
            wave::commit(ui, model::wavetable::resize(*inst->wavetable, n + dir * (big ? 16 : 1)), "SEGMENTS");
            return;
        case kCycles: {
            model::WavetableData d = *inst->wavetable;
            auto& c = d.segments[static_cast<std::size_t>(clamp_segment(*inst))].cycles;
            c = std::clamp(c + dir * (big ? 10 : 1), 1, 255);
            wave::commit(ui, std::move(d), "CYCLES");
            return;
        }
        case kLoopStart:
            w.loop_start = std::clamp(w.loop_start + dir, 0, n - 1);
            break;
        case kLoopLen:
            w.loop_length = std::clamp(w.loop_length + dir, 0, n - w.loop_start);
            break;
        case kXfade:
            w.crossfade = !w.crossfade;
            break;
        case kAuthentic:
            w.authentic = !w.authentic;
            break;
        case kTool:
            tool_ = (tool_ + dir + kTools) % kTools;
            return;
        default:
            return;
        }
        wave::commit_params(ui, w, "WAVE PARAMETERS");
    }

    int focus_ = kSegment;
    mutable int segment_ = 0;
    mutable int target_ = 0;
    int tool_ = kFree;
    bool drawing_ = false;
    int last_point_ = 0;
    float last_value_ = 0.0f;
    bool line_anchor_ = false;
    int anchor_point_ = 0;
    float anchor_value_ = 0.0f;
};

} // namespace

std::unique_ptr<Page> make_wavedraw_page() { return std::make_unique<WavedrawPage>(); }

} // namespace phos::ui
