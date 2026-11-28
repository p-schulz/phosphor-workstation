// Page R — REAL-TIME COMPOSER: drawing (input and editing in page_composer.cpp).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/notes.h"
#include "core/text.h"
#include "seq/timing.h"
#include "ui/pages/composer_page.h"
#include "ui/widgets.h"

namespace phos::ui::composer {

void ComposerPage::draw(gfx::Canvas& c, const Ui& ui) const {
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

std::string ComposerPage::value_text(int f, const Song& s) {
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

void ComposerPage::draw_grid(gfx::Canvas& c, const Ui& ui, const Song& song, bool fields) const {
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

void ComposerPage::draw_cursor_info(gfx::Canvas& c, const Ui& ui, const Song& song) const {
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

void ComposerPage::draw_order(gfx::Canvas& c, const Ui& ui, const Song& song, bool fields) const {
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

} // namespace phos::ui::composer
