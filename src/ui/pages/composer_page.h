#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "ui/layout.h"
#include "ui/pages/automation_view.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"

// Page R — REAL-TIME COMPOSER (docs/screens.md §3, PLAN.md §7.1), shared by page_composer.cpp
// (input and editing) and page_composer_draw.cpp (drawing). Internal to those two files.
namespace phos::ui::composer {

using model::Song;

enum Field : int {
    kSec, kPat, kSteps, kGrid, kBpm, kSig, kSwing, kMode, kMetro, kCount, kQuant, kRepeat, kFollow,
    kGridArea, kOrder, kPlay, kStop, kCont, kRec, kPanic, kFieldCount
};
constexpr int zone_of(int f) { return f * 16; }
constexpr int kZoneCell = 4000;  // + track * 256 + step (visible column)
constexpr int kZoneOrder = 9000; // + order index

constexpr int kGridTop = 6;   // rows 6..21: tracks 1..H
constexpr int kGridX = 104;   // px(13)
constexpr int kGridW = 384;
constexpr int kMaxVisible = 32;

struct Pos {
    int col;
    int row;
    int width;
    std::string_view label;
    int label_col;
};
// Header fields (label column, value column, width).
constexpr std::array<Pos, 13> kHeader = {{
    {5, 2, 2, "SEC", 1},     {12, 2, 3, "PAT", 8},     {35, 2, 4, "STEPS", 29},  {45, 2, 5, "GRID", 40},
    {5, 3, 6, "BPM", 1},     {16, 3, 5, "SIG", 12},    {28, 3, 4, "SWING", 22},  {38, 3, 7, "MODE", 33},
    {52, 3, 4, "METRO", 46}, {5, 4, 2, "CNT", 1},      {15, 4, 6, "QUANT", 9},   {30, 4, 3, "REPEAT", 23},
    {59, 2, 3, "FOLLOW", 52},
}};
constexpr std::array<model::Grid, 8> kGrids = {model::Grid::Quarter,  model::Grid::Eighth,     model::Grid::Sixteenth,
                                               model::Grid::ThirtySecond, model::Grid::SixtyFourth, model::Grid::EighthT,
                                               model::Grid::SixteenthT,   model::Grid::ThirtySecondT};

inline int grid_index(model::Grid g) {
    for (std::size_t i = 0; i < kGrids.size(); ++i) {
        if (kGrids[i] == g) {
            return static_cast<int>(i);
        }
    }
    return 2;
}

class ComposerPage final : public Page {
public:
    std::string_view id() const override { return "R"; }
    std::string_view title() const override { return "REAL-TIME COMPOSER"; }
    bool has_fields() const override { return true; }

    bool show(std::string_view view) override;
    void update(const InputState& in, Ui& ui) override;
    bool on_key(const InputEvent& ev, Ui& ui) override;
    void on_zone(int zone, int wheel, Ui& ui) override;
    void draw(gfx::Canvas& c, const Ui& ui) const override;

private:
    // page_composer.cpp
    void follow(Ui& ui);
    void clamp_cursor(const Song& song) const;
    const model::Note* note_at(const Song& song) const;
    bool grid_key(const InputEvent& ev, Ui& ui, bool big);
    bool grid_char(char ch, Ui& ui, bool big);
    template <typename Fn> void edit_note(Ui& ui, const char* label, Fn&& fn, double now = -10.0);
    void toggle_note(Ui& ui);
    void change_pitch(Ui& ui, int delta);
    void change(Ui& ui, int dir, bool big);
    void activate(Ui& ui);
    void edit_order(Ui& ui, bool insert);

    // page_composer_draw.cpp
    static std::string value_text(int f, const Song& s);
    void draw_grid(gfx::Canvas& c, const Ui& ui, const Song& song, bool fields) const;
    void draw_cursor_info(gfx::Canvas& c, const Ui& ui, const Song& song) const;
    void draw_order(gfx::Canvas& c, const Ui& ui, const Song& song, bool fields) const;

    int visible(const model::Pattern& p) const { return std::min(p.steps, kMaxVisible); }
    int cell_w(const model::Pattern& p) const { return kGridW / visible(p); }

    int focus_ = kGridArea;
    int track_ = 0;
    mutable int step_ = 0;
    mutable int view_ = 0;
    bool auto_ = false; // AUTO view (M8): CC lanes instead of the note grid
    AutomationView auto_view_;
    mutable int order_ = 0;
    std::array<int, model::kTracks> entry_pitch_ = [] {
        std::array<int, model::kTracks> a{};
        a.fill(60);
        return a;
    }();
};

} // namespace phos::ui::composer
