// Page R — REAL-TIME COMPOSER: input and editing (drawing in page_composer_draw.cpp). Section /
// pattern header, 16-track step grid with the playhead, transport, and the song order
// (docs/screens.md §3, PLAN.md §7.1). Every edit is undoable.

#include <algorithm>
#include <cstdlib>
#include <string>

#include "ui/pages/composer_page.h"

namespace phos::ui {

namespace composer {

template <typename Fn> void ComposerPage::edit_note(Ui& ui, const char* label, Fn&& fn, double now) {
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

bool ComposerPage::show(std::string_view view) {
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

void ComposerPage::update(const InputState& in, Ui& ui) {
    follow(ui);
    if (auto_) {
        const model::Pattern& p = ui.app().workspace().doc().song.pattern();
        auto_view_.update(in, ui, view_, visible(p));
    }
}

bool ComposerPage::on_key(const InputEvent& ev, Ui& ui) {
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

void ComposerPage::on_zone(int zone, int wheel, Ui& ui) {
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

// FOLLOW: while the song plays, Page R shows the playing section (SONG mode) and scrolls the
// grid a page at a time so the playhead stays in view; the cursor moves along with the page.
void ComposerPage::follow(Ui& ui) {
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

void ComposerPage::clamp_cursor(const Song& song) const {
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

const model::Note* ComposerPage::note_at(const Song& song) const {
    const model::Pattern& p = song.pattern();
    const std::uint32_t from = static_cast<std::uint32_t>(step_) * p.step_ticks();
    for (const auto& n : p.lanes[static_cast<std::size_t>(track_)]) {
        if (n.tick >= from && n.tick < from + p.step_ticks()) {
            return &n;
        }
    }
    return nullptr;
}

bool ComposerPage::grid_key(const InputEvent& ev, Ui& ui, bool big) {
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

bool ComposerPage::grid_char(char ch, Ui& ui, bool big) {
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

void ComposerPage::toggle_note(Ui& ui) {
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

void ComposerPage::change_pitch(Ui& ui, int delta) {
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

void ComposerPage::change(Ui& ui, int dir, bool big) {
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

void ComposerPage::activate(Ui& ui) {
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

void ComposerPage::edit_order(Ui& ui, bool insert) {
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

} // namespace composer

std::unique_ptr<Page> make_composer_page() { return std::make_unique<composer::ComposerPage>(); }

} // namespace phos::ui
