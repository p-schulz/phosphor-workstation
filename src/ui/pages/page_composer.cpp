// Page R — REAL-TIME COMPOSER: section / pattern header, 16-track step grid with the playhead,
// transport, and the song order (docs/screens.md §3, PLAN.md §7.1). Every edit is undoable.

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/notes.h"
#include "core/text.h"
#include "seq/timing.h"
#include "ui/layout.h"
#include "ui/pages/automation_view.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

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

int grid_index(model::Grid g) {
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

    bool show(std::string_view view) override {
        // "AUTO" (optionally with a controller: "AUTO 74") or "NOTES".
        if (view == "NOTES") {
            auto_ = false;
            return true;
        }
        if (view.substr(0, 4) != "AUTO") {
            return false;
        }
        auto_ = true;
        if (view.size() > 5) {
            const int cc = std::atoi(std::string(view.substr(5)).c_str());
            auto_view_.cc = std::clamp(cc, 0, 127);
        }
        return true;
    }

    void update(const InputState& in, Ui& ui) override {
        follow(ui);
        if (auto_) {
            const model::Pattern& p = ui.app().workspace().doc().song.pattern();
            auto_view_.update(in, ui, view_, visible(p));
        }
    }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type == InputEvent::Type::Text) {
            return ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_'
                       ? (change(ui, (ev.ch == '+' || ev.ch == '=') ? 1 : -1, false), true)
                       : false;
        }
        const bool big = (ev.mods & kModShift) != 0;
        const Song& song = ui.app().workspace().doc().song;
        if (focus_ == kGridArea && auto_) {
            const bool used = auto_view_.on_key(ev, ui, step_, big);
            clamp_cursor(song);
            if (used) {
                return true;
            }
            if (ev.key == Key::Tab || ev.key == Key::Escape) {
                focus_ = kOrder;
                return true;
            }
            return false;
        }
        if (focus_ == kGridArea) {
            return grid_key(ev, ui, big);
        }
        switch (ev.key) {
        case Key::Up:
            if (focus_ == 0) {
                ui.set_focus(Focus::Command);
            } else if (focus_ == kOrder) {
                focus_ = kGridArea;
                track_ = model::kTracks - 1;
            } else {
                --focus_;
            }
            return true;
        case Key::Down:
        case Key::Tab:
            if (focus_ == kFollow) {
                focus_ = kGridArea;
                track_ = song.current_track;
            } else {
                focus_ = std::min(focus_ + 1, kFieldCount - 1);
            }
            return true;
        case Key::Left:
        case Key::Right:
            if (focus_ == kOrder) {
                order_ = std::clamp(order_ + (ev.key == Key::Left ? -1 : 1), 0, static_cast<int>(song.order.size()) - 1);
            } else {
                change(ui, ev.key == Key::Left ? -1 : 1, big);
            }
            return true;
        case Key::Enter:
            activate(ui);
            return true;
        case Key::Insert:
        case Key::Delete:
        case Key::Backspace:
            if (focus_ == kOrder) {
                edit_order(ui, ev.key == Key::Insert);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (auto_ && auto_view_.on_zone(zone)) {
            return;
        }
        if (zone >= kZoneOrder) {
            focus_ = kOrder;
            order_ = zone - kZoneOrder;
            return;
        }
        if (zone >= kZoneCell) {
            const int track = (zone - kZoneCell) / 256;
            const int step = view_ + (zone - kZoneCell) % 256;
            const bool same = focus_ == kGridArea && track == track_ && step == step_;
            focus_ = kGridArea;
            track_ = track;
            step_ = step;
            ui.app().workspace().select_track(track);
            if (same) {
                toggle_note(ui);
            }
            return;
        }
        focus_ = std::clamp(zone / 16, 0, kFieldCount - 1);
        if (wheel != 0) {
            change(ui, wheel, false);
        } else if (focus_ >= kPlay) {
            activate(ui);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const Song& song = ui.app().workspace().doc().song;
        const bool fields = ui.focus() == Focus::Fields;
        const model::Pattern& pat = song.pattern();
        clamp_cursor(song);

        for (int f = 0; f < static_cast<int>(kHeader.size()); ++f) {
            const Pos& p = kHeader[static_cast<std::size_t>(f)];
            widgets::text(c, ui, p.label_col, p.row, p.label, gfx::kInkDim);
            const std::string value = f == kFollow ? (ui.app().follow() ? "ON" : "OFF") : value_text(f, song);
            widgets::value_field(c, ui, p.col, p.row, p.width, value, fields && focus_ == f, zone_of(f));
        }
        widgets::small_text(c, ui, px(16), py(2) + 3, fit(to_upper(pat.name), 12), gfx::kInkText);
        const auto& tr = song.tracks[static_cast<std::size_t>(song.current_track)];
        const auto& inst = ui.app().workspace().doc().instruments[static_cast<std::size_t>(tr.instrument)];
        char buf[96];
        std::snprintf(buf, sizeof buf, "TRACK %c  INST %02d %s", model::track_label(song.current_track), tr.instrument + 1,
                      fit(inst.name, 10).c_str());
        widgets::small_text(c, ui, px(35), py(4) + 3, buf, gfx::kInkText);

        if (auto_) {
            auto_view_.draw(c, ui, view_, visible(song.pattern()), step_, fields && focus_ == kGridArea);
        } else {
            draw_grid(c, ui, song, fields);
            draw_cursor_info(c, ui, song);
        }

        // Transport.
        constexpr std::array<std::string_view, 4> kButtons = {"PLAY", "STOP", "CONT", "REC"};
        int col = 1;
        for (int b = 0; b < 4; ++b) {
            widgets::button(c, ui, col, 23, kButtons[static_cast<std::size_t>(b)], fields && focus_ == kPlay + b,
                            zone_of(kPlay + b));
            col += static_cast<int>(kButtons[static_cast<std::size_t>(b)].size()) + 3;
        }
        const StatusInfo& st = ui.app().status();
        // Section and position are in the status header (A001:1:000).
        std::snprintf(buf, sizeof buf, "%s  STEP %d/%d", st.recording ? "RECORDING" : st.count_in ? "COUNT-IN" : st.playing ? "PLAYING" : "STOPPED",
                      st.song_step + 1, static_cast<int>(song.order.size()));
        widgets::text(c, ui, 27, 23, buf, st.recording ? gfx::kInkWarn : st.playing ? gfx::kInkBright : gfx::kInkDim);
        widgets::button(c, ui, 57, 23, "PANIC", fields && focus_ == kPanic, zone_of(kPanic));

        draw_order(c, ui, song, fields);
        widgets::small_text(c, ui, px(1), py(27) + 2,
                            auto_ ? "AUTO: PEN DRAWS  LEFT/RIGHT STEP  UP/DOWN VALUE  DEL CLEARS  PG UP/DN CC"
                                  : "GRID: ENTER NOTE  +/- PITCH  [ ] LENGTH  , . VELOCITY  DEL  M S W = MUTE SOLO SWING",
                            gfx::kInkDim);
    }

private:
    // FOLLOW: while the song plays, Page R shows the playing section (SONG mode) and scrolls the
    // grid a page at a time so the playhead stays in view; the cursor moves along with the page.
    void follow(Ui& ui) {
        const StatusInfo& st = ui.app().status();
        if (!ui.app().follow() || !st.playing || st.count_in) {
            return;
        }
        model::Workspace& ws = ui.app().workspace();
        if (ws.doc().song.play_mode == model::PlayMode::Song && st.section != ws.doc().song.current_section) {
            ws.select_section(st.section);
        }
        const Song& song = ws.doc().song;
        const model::Pattern& p = song.pattern();
        if (st.pattern != song.section().pattern || p.step_ticks() == 0) {
            return;
        }
        const int vis = visible(p);
        const int play_step = std::min(static_cast<int>(st.pattern_tick / p.step_ticks()), p.steps - 1);
        const int page = std::clamp(play_step / vis * vis, 0, std::max(0, p.steps - vis));
        if (page != view_) {
            step_ = std::clamp(page + (step_ - view_), page, page + vis - 1);
            view_ = page;
        }
    }

    int visible(const model::Pattern& p) const { return std::min(p.steps, kMaxVisible); }
    int cell_w(const model::Pattern& p) const { return kGridW / visible(p); }

    void clamp_cursor(const Song& song) const {
        const model::Pattern& p = song.pattern();
        step_ = std::clamp(step_, 0, p.steps - 1);
        if (step_ < view_) {
            view_ = step_;
        } else if (step_ >= view_ + visible(p)) {
            view_ = step_ - visible(p) + 1;
        }
        view_ = std::clamp(view_, 0, std::max(0, p.steps - visible(p)));
        order_ = std::clamp(order_, 0, static_cast<int>(song.order.size()) - 1);
    }

    static std::string value_text(int f, const Song& s) {
        char buf[24];
        const auto& sec = s.section();
        switch (f) {
        case kSec:
            return std::string(1, static_cast<char>('A' + s.current_section));
        case kPat:
            std::snprintf(buf, sizeof buf, "%02d", sec.pattern + 1);
            return buf;
        case kSteps:
            std::snprintf(buf, sizeof buf, "%3d", s.pattern().steps);
            return buf;
        case kGrid:
            return model::grid_name(s.pattern().grid);
        case kBpm:
            std::snprintf(buf, sizeof buf, "%5.1f", sec.bpm10 / 10.0);
            return buf;
        case kSig:
            std::snprintf(buf, sizeof buf, "%d/%d", sec.sig_num, sec.sig_den);
            return buf;
        case kSwing:
            std::snprintf(buf, sizeof buf, "%d%%", s.swing);
            return buf;
        case kMode:
            return s.play_mode == model::PlayMode::Song ? "SONG" : "PATTERN";
        case kMetro: {
            constexpr std::array<const char*, 3> kMetroNames = {"OFF", "PLAY", "REC"};
            return kMetroNames[static_cast<std::size_t>(s.metronome)];
        }
        case kCount:
            return std::to_string(s.count_in_bars);
        case kQuant:
            return s.quantize_on_record ? model::grid_name(s.record_quantize) : "OFF";
        case kRepeat:
            std::snprintf(buf, sizeof buf, "%2d", sec.repeats);
            return buf;
        default:
            return {};
        }
    }

    void draw_grid(gfx::Canvas& c, const Ui& ui, const Song& song, bool fields) const {
        const model::Pattern& p = song.pattern();
        const int cw = cell_w(p);
        const auto step_ticks = static_cast<double>(p.step_ticks());
        const auto beat_ticks = static_cast<std::uint32_t>(seq::beat_ticks(song.section().sig_den));
        const auto bar_ticks = static_cast<std::uint32_t>(seq::bar_ticks(song.section().sig_num, song.section().sig_den));
        const int rows_h = model::kTracks * kCellH;
        const auto x_of_tick = [&](double tick) {
            return kGridX + static_cast<int>(std::lround((tick / step_ticks - view_) * cw));
        };
        // Ruler: step numbers at every beat.
        for (int s = view_; s < view_ + visible(p); ++s) {
            const auto tick = static_cast<std::uint32_t>(s) * p.step_ticks();
            if (tick % beat_ticks == 0) {
                widgets::small_text(c, ui, x_of_tick(tick) + 1, py(5) + 3, std::to_string(s + 1),
                                    tick % bar_ticks == 0 ? gfx::kInkText : gfx::kInkDim);
                c.dotted_line(x_of_tick(tick), py(kGridTop), x_of_tick(tick), py(kGridTop) + rows_h - 1, 2,
                              tick % bar_ticks == 0 ? gfx::kInkDim : gfx::kInkGrid);
            }
        }
        const model::Document& doc = ui.app().workspace().doc();
        for (int t = 0; t < model::kTracks; ++t) {
            const int row = kGridTop + t;
            const int y = py(row);
            const auto& tr = song.tracks[static_cast<std::size_t>(t)];
            const bool current = t == song.current_track;
            widgets::text(c, ui, 0, row, std::string(1, model::track_label(t)), current ? gfx::kInkBright : gfx::kInkDim);
            char name[16];
            std::snprintf(name, sizeof name, "%02d %s", tr.instrument + 1,
                          fit(doc.instruments[static_cast<std::size_t>(tr.instrument)].name, 8).c_str());
            widgets::small_text(c, ui, px(2) - 4, y + 3, name, current ? gfx::kInkText : gfx::kInkDim);
            std::string flags;
            flags += tr.mute ? 'M' : ' ';
            flags += tr.solo ? 'S' : ' ';
            flags += tr.swing ? 'W' : ' ';
            widgets::small_text(c, ui, px(11) - 6, y + 3, flags, tr.mute ? gfx::kInkWarn : gfx::kInkBright);
            for (int s = 0; s < visible(p); ++s) {
                c.set(kGridX + s * cw + cw / 2, y + kCellH / 2, gfx::kInkGrid);
                ui.add_zone(gfx::Rect{kGridX + s * cw, y, cw, kCellH}, ZoneOwner::Page, kZoneCell + t * 256 + s);
            }
            for (const auto& n : p.lanes[static_cast<std::size_t>(t)]) {
                const double start = static_cast<double>(n.tick);
                const double end = static_cast<double>(n.tick + n.length);
                if (end / step_ticks <= view_ || start / step_ticks >= view_ + visible(p)) {
                    continue;
                }
                const int x0 = std::max(kGridX, x_of_tick(start));
                const int x1 = std::min(kGridX + kGridW - 1, x_of_tick(end) - 1);
                c.fill_rect(gfx::Rect{x0, y + 3, std::max(2, std::min(cw - 2, x1 - x0)), kCellH - 6}, gfx::kInkText);
                c.hline(x0, x1, y + kCellH / 2, gfx::kInkText);
                c.vline(x1, y + 3, y + kCellH - 4, gfx::kInkText);
            }
        }
        c.rect(gfx::Rect{kGridX - 1, py(kGridTop) - 1, kGridW + 2, rows_h + 2}, gfx::kInkDim);
        // Playhead while this pattern plays.
        const StatusInfo& st = ui.app().status();
        if (st.playing && !st.count_in && st.pattern == song.section().pattern) {
            const int x = x_of_tick(st.pattern_tick);
            if (x >= kGridX && x < kGridX + kGridW) {
                c.vline(x, py(kGridTop), py(kGridTop) + rows_h - 1, gfx::kInkAccent);
            }
        }
        if (fields && focus_ == kGridArea) {
            const gfx::Rect cur{kGridX + (step_ - view_) * cw, py(kGridTop + track_), cw, kCellH};
            c.rect(cur, gfx::kInkCursor);
        }
        if (p.steps > kMaxVisible) {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%d-%d/%d", view_ + 1, view_ + visible(p), p.steps);
            widgets::small_text(c, ui, px(53), py(5) + 3, buf, gfx::kInkDim);
        }
    }

    const model::Note* note_at(const Song& song) const {
        const model::Pattern& p = song.pattern();
        const std::uint32_t from = static_cast<std::uint32_t>(step_) * p.step_ticks();
        for (const auto& n : p.lanes[static_cast<std::size_t>(track_)]) {
            if (n.tick >= from && n.tick < from + p.step_ticks()) {
                return &n;
            }
        }
        return nullptr;
    }

    void draw_cursor_info(gfx::Canvas& c, const Ui& ui, const Song& song) const {
        char buf[96];
        const model::Note* n = note_at(song);
        if (n) {
            std::snprintf(buf, sizeof buf, "TRACK %c STEP %03d  %s  VEL %3d  LEN %.2f STEPS", model::track_label(track_),
                          step_ + 1, note_name(n->note).c_str(), n->velocity,
                          static_cast<double>(n->length) / song.pattern().step_ticks());
        } else {
            std::snprintf(buf, sizeof buf, "TRACK %c STEP %03d  EMPTY (ENTER = %s)", model::track_label(track_), step_ + 1,
                          note_name(entry_pitch_[static_cast<std::size_t>(track_)]).c_str());
        }
        widgets::text(c, ui, 1, 22, buf, focus_ == kGridArea ? gfx::kInkText : gfx::kInkDim);
    }

    void draw_order(gfx::Canvas& c, const Ui& ui, const Song& song, bool fields) const {
        widgets::text(c, ui, 1, 25, "ORDER", gfx::kInkDim);
        for (int i = 0; i < static_cast<int>(song.order.size()) && i < 26; ++i) {
            const int col = 8 + i * 2;
            const char letter = static_cast<char>('A' + song.order[static_cast<std::size_t>(i)]);
            const bool cur = fields && focus_ == kOrder && i == order_;
            const bool playing = ui.app().status().playing && song.play_mode == model::PlayMode::Song &&
                                 ui.app().status().song_step == i;
            if (cur || playing) {
                c.fill_rect(cells(col, 25, 1, 1), cur ? gfx::kInkBright : gfx::kInkField);
            }
            widgets::text(c, ui, col, 25, std::string(1, letter), cur ? gfx::kInkBg : gfx::kInkText);
            ui.add_zone(cells(col, 25, 1, 1), ZoneOwner::Page, kZoneOrder + i);
        }
        if (song.order.size() > 26) {
            widgets::small_text(c, ui, px(60), py(25) + 3, "+" + std::to_string(song.order.size() - 26), gfx::kInkDim);
        }
        widgets::small_text(c, ui, px(1), py(26) + 2,
                            "ORDER: LEFT/RIGHT MOVE, +/- SECTION, INS ADD STEP, DEL REMOVE.  MODE SONG PLAYS IT.",
                            gfx::kInkDim);
    }

    bool grid_key(const InputEvent& ev, Ui& ui, bool big) {
        const Song& song = ui.app().workspace().doc().song;
        switch (ev.key) {
        case Key::Up:
            if (track_ == 0) {
                focus_ = kFollow;
            } else {
                ui.app().workspace().select_track(--track_);
            }
            return true;
        case Key::Down:
            if (track_ == model::kTracks - 1) {
                focus_ = kOrder;
            } else {
                ui.app().workspace().select_track(++track_);
            }
            return true;
        case Key::Left:
        case Key::Right:
            step_ = std::clamp(step_ + (ev.key == Key::Left ? -1 : 1) * (big ? 4 : 1), 0, song.pattern().steps - 1);
            return true;
        case Key::Home:
            step_ = 0;
            return true;
        case Key::End:
            step_ = song.pattern().steps - 1;
            return true;
        case Key::Enter:
            toggle_note(ui);
            return true;
        case Key::Delete:
        case Key::Backspace:
            edit_note(ui, "DELETE NOTE", [](Song&, std::vector<model::Note>& lane, model::Note* n) {
                if (n) {
                    lane.erase(lane.begin() + (n - lane.data()));
                }
            });
            return true;
        case Key::Tab:
            focus_ = kOrder;
            return true;
        case Key::Char:
            return grid_char(ev.ch, ui, big);
        default:
            return false;
        }
    }

    bool grid_char(char ch, Ui& ui, bool big) {
        const double now = ui.app().time_s();
        auto track_flag = [&](const char* label, bool model::Track::*flag) {
            Song s = ui.app().workspace().doc().song;
            auto& t = s.tracks[static_cast<std::size_t>(track_)];
            t.*flag = !(t.*flag);
            ui.app().workspace().set_song(s, label);
        };
        switch (ch) {
        case 'M':
            track_flag("MUTE", &model::Track::mute);
            return true;
        case 'S':
            track_flag("SOLO", &model::Track::solo);
            return true;
        case 'W':
            track_flag("SWING TRACK", &model::Track::swing);
            return true;
        case '[':
        case ']': {
            const int dir = ch == ']' ? 1 : -1;
            edit_note(ui, "NOTE LENGTH", [&](Song& s, std::vector<model::Note>&, model::Note* n) {
                if (n) {
                    const auto st = static_cast<int>(s.pattern().step_ticks());
                    n->length = static_cast<std::uint32_t>(std::max(st / 4, static_cast<int>(n->length) + dir * st));
                }
            }, now);
            return true;
        }
        case ',':
        case '.': {
            const int dir = ch == '.' ? 1 : -1;
            edit_note(ui, "NOTE VELOCITY", [&](Song&, std::vector<model::Note>&, model::Note* n) {
                if (n) {
                    n->velocity = static_cast<std::uint8_t>(std::clamp(n->velocity + dir * (big ? 1 : 10), 1, 127));
                }
            }, now);
            return true;
        }
        default:
            return false;
        }
    }

    template <typename Fn> void edit_note(Ui& ui, const char* label, Fn&& fn, double now = -10.0) {
        Song s = ui.app().workspace().doc().song;
        model::Pattern& p = s.pattern();
        auto& lane = p.lanes[static_cast<std::size_t>(track_)];
        const std::uint32_t from = static_cast<std::uint32_t>(step_) * p.step_ticks();
        model::Note* n = nullptr;
        for (auto& x : lane) {
            if (x.tick >= from && x.tick < from + p.step_ticks()) {
                n = &x;
                break;
            }
        }
        fn(s, lane, n);
        ui.app().workspace().set_song(s, label, now);
    }

    void toggle_note(Ui& ui) {
        const Song& song = ui.app().workspace().doc().song;
        if (note_at(song)) {
            edit_note(ui, "DELETE NOTE", [](Song&, std::vector<model::Note>& lane, model::Note* n) {
                lane.erase(lane.begin() + (n - lane.data()));
            });
            return;
        }
        Song s = song;
        model::Pattern& p = s.pattern();
        model::insert_note(p.lanes[static_cast<std::size_t>(track_)],
                           model::Note{static_cast<std::uint32_t>(step_) * p.step_ticks(), p.step_ticks(),
                                       static_cast<std::uint8_t>(entry_pitch_[static_cast<std::size_t>(track_)]), 100});
        ui.app().workspace().set_song(s, "NOTE");
        // Hear it: the track's instrument at the new pitch.
        ui.app().keyjazz_note(entry_pitch_[static_cast<std::size_t>(track_)], true);
        ui.app().keyjazz_note(entry_pitch_[static_cast<std::size_t>(track_)], false);
    }

    void change_pitch(Ui& ui, int delta) {
        const Song& song = ui.app().workspace().doc().song;
        int& entry = entry_pitch_[static_cast<std::size_t>(track_)];
        if (!note_at(song)) {
            entry = std::clamp(entry + delta, 0, 127);
            return;
        }
        edit_note(ui, "NOTE PITCH", [&](Song&, std::vector<model::Note>&, model::Note* n) {
            n->note = static_cast<std::uint8_t>(std::clamp(n->note + delta, 0, 127));
            entry = n->note;
        }, ui.app().time_s());
    }

    void change(Ui& ui, int dir, bool big) {
        model::Workspace& ws = ui.app().workspace();
        if (focus_ == kGridArea) {
            change_pitch(ui, dir * (big ? 12 : 1));
            return;
        }
        if (focus_ == kOrder) {
            Song s = ws.doc().song;
            int& sec = s.order[static_cast<std::size_t>(order_)];
            sec = (sec + dir + model::kSections) % model::kSections;
            ws.set_song(s, "SONG ORDER", ui.app().time_s());
            return;
        }
        if (focus_ == kSec) {
            ws.select_section((ws.doc().song.current_section + dir + model::kSections) % model::kSections);
            return;
        }
        Song s = ws.doc().song;
        model::Section& sec = s.sections[static_cast<std::size_t>(s.current_section)];
        model::Pattern& p = s.pattern();
        const char* label = "";
        switch (focus_) {
        case kPat:
            sec.pattern = (sec.pattern + dir + static_cast<int>(s.patterns.size())) % static_cast<int>(s.patterns.size());
            label = "SECTION PATTERN";
            break;
        case kSteps:
            p.steps = std::clamp(p.steps + dir * (big ? 16 : 1), 1, 256);
            label = "STEPS";
            break;
        case kGrid:
            p.grid = kGrids[static_cast<std::size_t>((grid_index(p.grid) + dir + 8) % 8)];
            label = "GRID";
            break;
        case kBpm:
            sec.bpm10 = std::clamp(sec.bpm10 + dir * (big ? 10 : 1), 200, 3000);
            label = "TEMPO";
            break;
        case kSig:
            if (big) {
                constexpr std::array<int, 4> kDens = {2, 4, 8, 16};
                const auto it = std::find(kDens.begin(), kDens.end(), sec.sig_den);
                sec.sig_den = kDens[static_cast<std::size_t>(((it - kDens.begin()) + dir + 4) % 4)];
            } else {
                sec.sig_num = std::clamp(sec.sig_num + dir, 1, 16);
            }
            label = "SIGNATURE";
            break;
        case kSwing:
            s.swing = std::clamp(s.swing + dir, 50, 75);
            label = "SWING";
            break;
        case kMode:
            s.play_mode = s.play_mode == model::PlayMode::Song ? model::PlayMode::Pattern : model::PlayMode::Song;
            label = "PLAY MODE";
            break;
        case kMetro:
            s.metronome = static_cast<model::MetronomeMode>((static_cast<int>(s.metronome) + dir + 3) % 3);
            label = "METRONOME";
            break;
        case kCount:
            s.count_in_bars = std::clamp(s.count_in_bars + dir, 0, 2);
            label = "COUNT-IN";
            break;
        case kQuant: {
            const int i = s.quantize_on_record ? grid_index(s.record_quantize) + 1 : 0; // 0 = OFF
            const int next = (i + dir + 9) % 9;
            s.quantize_on_record = next > 0;
            if (next > 0) {
                s.record_quantize = kGrids[static_cast<std::size_t>(next - 1)];
            }
            label = "QUANTIZE";
            break;
        }
        case kRepeat:
            sec.repeats = std::clamp(sec.repeats + dir, 1, 99);
            label = "REPEATS";
            break;
        case kFollow:
            ui.execute("FOLLOW");
            return;
        default:
            return;
        }
        ws.set_song(s, label, ui.app().time_s());
    }

    void activate(Ui& ui) {
        switch (focus_) {
        case kPlay:
            ui.execute("PLAY");
            break;
        case kStop:
            ui.execute("STOP");
            break;
        case kCont:
            ui.execute("CONTINUE");
            break;
        case kRec:
            ui.execute("REC");
            break;
        case kPanic:
            ui.execute("PANIC");
            break;
        case kOrder:
            break;
        default:
            change(ui, 1, false);
            break;
        }
    }

    void edit_order(Ui& ui, bool insert) {
        Song s = ui.app().workspace().doc().song;
        if (insert) {
            if (static_cast<int>(s.order.size()) >= model::kMaxSongSteps) {
                return;
            }
            s.order.insert(s.order.begin() + order_ + 1, s.order[static_cast<std::size_t>(order_)]);
            ++order_;
        } else {
            if (s.order.size() <= 1) {
                return;
            }
            s.order.erase(s.order.begin() + order_);
        }
        ui.app().workspace().set_song(s, "SONG ORDER");
    }

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

} // namespace

std::unique_ptr<Page> make_composer_page() { return std::make_unique<ComposerPage>(); }

} // namespace phos::ui
