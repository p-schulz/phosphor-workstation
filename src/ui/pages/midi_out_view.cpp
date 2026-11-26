#include "ui/pages/midi_out_view.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "core/text.h"
#include "ui/layout.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

constexpr int kOutRows = 5;                       // outputs listed
constexpr int kOutFields = kOutRows * 2;          // enable, clock
constexpr int kMidiOut = kOutFields;              // settings
constexpr int kThru = kMidiOut + 1;
constexpr int kOffset = kMidiOut + 2;
constexpr int kComp = kMidiOut + 3;
constexpr int kTrackBase = kMidiOut + 4;          // + track * 4 + (dest, port, ch, prog)
constexpr int kFieldCount = kTrackBase + model::kTracks * 4;

constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};

int zone(int field, int option = 0) { return MidiOutView::kZoneBase + field * 4 + option; }

// Ports a track can choose: FIRST (the first open output), then every known output.
std::vector<std::string> port_choices(const Ui& ui) {
    std::vector<std::string> v{""};
    for (const auto& o : ui.app().midi().outputs) {
        v.push_back(o.name);
    }
    return v;
}

} // namespace

bool MidiOutView::on_key(const InputEvent& ev, Ui& ui) {
    if (ev.type == InputEvent::Type::Text) {
        if (ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_') {
            change(ui, focus_, (ev.ch == '+' || ev.ch == '=') ? 1 : -1);
            return true;
        }
        return false;
    }
    switch (ev.key) {
    case Key::Up:
        if (focus_ == 0) {
            ui.set_focus(Focus::Command);
        } else {
            focus_ = focus_ >= kTrackBase + 4 ? focus_ - 4 : std::max(0, focus_ - 1);
        }
        return true;
    case Key::Down:
        focus_ = focus_ >= kTrackBase ? std::min(kFieldCount - 1, focus_ + 4) : focus_ + 1;
        return true;
    case Key::Tab:
        focus_ = (focus_ + 1) % kFieldCount;
        return true;
    case Key::Left:
    case Key::Right: {
        const int dir = ev.key == Key::Right ? 1 : -1;
        if (focus_ >= kTrackBase && (ev.mods & kModShift)) { // SHIFT: move between the columns
            focus_ = std::clamp(focus_ + dir, static_cast<int>(kTrackBase), kFieldCount - 1);
        } else {
            change(ui, focus_, dir * ((ev.mods & kModShift) ? 10 : 1));
        }
        return true;
    }
    case Key::Enter:
        change(ui, focus_, 1);
        return true;
    default:
        return false;
    }
}

void MidiOutView::on_zone(int z, int wheel, Ui& ui) {
    const int field = (z - kZoneBase) / 4;
    const int option = (z - kZoneBase) % 4;
    if (field < 0 || field >= kFieldCount) {
        return;
    }
    focus_ = field;
    if (wheel != 0) {
        change(ui, field, wheel);
        return;
    }
    const bool toggle = field < kTrackBase && field != kOffset;
    if (toggle) {
        // Option groups: the option clicked (OFF = 0, ON = 1).
        MidiOutConfig c = ui.app().midi_out_config();
        const bool on = option == 1;
        if (field < kOutFields) {
            const auto& outs = ui.app().midi().outputs;
            const std::size_t row = static_cast<std::size_t>(field / 2);
            if (row >= outs.size()) {
                return;
            }
            ui.execute("MIDIOUT " + std::string(field % 2 == 0 ? "PORT " : "CLOCK ") + std::to_string(row + 1) +
                       (on ? " ON" : " OFF"));
            return;
        }
        (field == kMidiOut ? c.enabled : field == kThru ? c.thru : c.latency_comp) = on;
        ui.app().set_midi_out_config(c);
        return;
    }
    change(ui, field, 1);
}

void MidiOutView::change(Ui& ui, int field, int dir) const {
    MidiOutConfig c = ui.app().midi_out_config();
    if (field < kOutFields) {
        const auto& outs = ui.app().midi().outputs;
        const std::size_t row = static_cast<std::size_t>(field / 2);
        if (row < outs.size()) {
            const bool on = field % 2 == 0 ? outs[row].enabled : outs[row].clock;
            ui.execute("MIDIOUT " + std::string(field % 2 == 0 ? "PORT " : "CLOCK ") + std::to_string(row + 1) +
                       (on ? " OFF" : " ON"));
        }
        return;
    }
    if (field < kTrackBase) {
        if (field == kOffset) {
            c.offset_ms = std::clamp(c.offset_ms + dir, -100, 100);
        } else {
            bool& b = field == kMidiOut ? c.enabled : field == kThru ? c.thru : c.latency_comp;
            b = !b;
        }
        ui.app().set_midi_out_config(c);
        return;
    }
    model::Workspace& ws = ui.app().workspace();
    model::Song s = ws.doc().song;
    const int t = (field - kTrackBase) / 4;
    auto& tr = s.tracks[static_cast<std::size_t>(t)];
    const int step = dir > 0 ? 1 : -1;
    switch ((field - kTrackBase) % 4) {
    case 0:
        tr.dest = static_cast<model::MidiDest>((static_cast<int>(tr.dest) + 3 + step) % 3);
        break;
    case 1: {
        const auto ports = port_choices(ui);
        auto it = std::find(ports.begin(), ports.end(), tr.out_port);
        const int i = it == ports.end() ? 0 : static_cast<int>(it - ports.begin());
        const int n = static_cast<int>(ports.size());
        tr.out_port = ports[static_cast<std::size_t>(((i + step) % n + n) % n)];
        break;
    }
    case 2:
        tr.out_channel = std::clamp(tr.out_channel + dir, 0, 15);
        break;
    default:
        tr.program = std::clamp(tr.program + dir, -1, 127);
        break;
    }
    ws.set_song(s, "TRACK MIDI OUT", ui.app().time_s());
}

void MidiOutView::draw(gfx::Canvas& c, const Ui& ui, bool fields) const {
    const MidiInfo& mi = ui.app().midi();
    const MidiOutConfig& cfg = ui.app().midi_out_config();
    const auto focused = [&](int f) { return fields && focus_ == f; };

    widgets::frame(c, ui, gfx::Rect{2, py(2) + 6, kCanvasW - 4, py(10) - 3 - (py(2) + 6)}, "MIDI OUTPUTS");
    widgets::small_text(c, ui, px(2), py(3) + 2, "OUT", gfx::kInkDim);
    widgets::small_text(c, ui, px(10), py(3) + 2, "NAME", gfx::kInkDim);
    widgets::small_text(c, ui, px(38), py(3) + 2, "CLOCK", gfx::kInkDim);
    widgets::small_text(c, ui, px(47), py(3) + 2, "STATE", gfx::kInkDim);
    if (!mi.available) {
        widgets::text(c, ui, 10, 4, "NO MIDI (HEADLESS)", gfx::kInkDim);
    } else if (mi.outputs.empty()) {
        widgets::text(c, ui, 10, 4, "NO MIDI OUTPUTS FOUND", gfx::kInkDim);
    }
    for (int i = 0; i < kOutRows && i < static_cast<int>(mi.outputs.size()); ++i) {
        const auto& o = mi.outputs[static_cast<std::size_t>(i)];
        const int row = 4 + i;
        widgets::option_group(c, ui, 2, row, kOffOn, o.enabled ? 1 : 0, focused(i * 2), zone(i * 2));
        widgets::text(c, ui, 10, row, fit(to_upper(o.name), 26), o.present ? gfx::kInkText : gfx::kInkDim);
        widgets::option_group(c, ui, 38, row, kOffOn, o.clock ? 1 : 0, focused(i * 2 + 1), zone(i * 2 + 1));
        const std::string state = !o.error.empty() ? o.error : !o.present ? "MISSING" : o.open ? "OPEN" : "OFF";
        widgets::text(c, ui, 47, row, fit(state, 15), o.open ? gfx::kInkPlay : !o.error.empty() ? gfx::kInkWarn : gfx::kInkDim);
    }

    widgets::frame(c, ui, gfx::Rect{2, py(10) + 6, kCanvasW - 4, py(14) - 3 - (py(10) + 6)}, "OUTPUT");
    widgets::label(c, ui, 2, 11, "MIDI OUT", 10);
    widgets::option_group(c, ui, 14, 11, kOffOn, cfg.enabled ? 1 : 0, focused(kMidiOut), zone(kMidiOut));
    widgets::label(c, ui, 34, 11, "THRU", 12);
    widgets::option_group(c, ui, 48, 11, kOffOn, cfg.thru ? 1 : 0, focused(kThru), zone(kThru));
    widgets::label(c, ui, 2, 12, "OFFSET MS", 10);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%+4d", cfg.offset_ms);
    widgets::value_field(c, ui, 14, 12, 4, buf, focused(kOffset), zone(kOffset));
    widgets::label(c, ui, 34, 12, "LAT. COMP", 12);
    widgets::option_group(c, ui, 48, 12, kOffOn, cfg.latency_comp ? 1 : 0, focused(kComp), zone(kComp));

    widgets::frame(c, ui, gfx::Rect{2, py(14) + 6, kCanvasW - 4, py(29) - 3 - (py(14) + 6)}, "TRACKS");
    const model::Song& song = ui.app().workspace().doc().song;
    for (int half = 0; half < 2; ++half) {
        const int x = half == 0 ? 2 : 33;
        widgets::small_text(c, ui, px(x), py(15) + 2, "TR", gfx::kInkDim);
        widgets::small_text(c, ui, px(x + 2), py(15) + 2, "DEST", gfx::kInkDim);
        widgets::small_text(c, ui, px(x + 7), py(15) + 2, "PORT", gfx::kInkDim);
        widgets::small_text(c, ui, px(x + 18), py(15) + 2, "CH", gfx::kInkDim);
        widgets::small_text(c, ui, px(x + 22), py(15) + 2, "PROG", gfx::kInkDim);
        for (int k = 0; k < 8; ++k) {
            const int t = half * 8 + k;
            const int row = 16 + k;
            const auto& tr = song.tracks[static_cast<std::size_t>(t)];
            const int f = kTrackBase + t * 4;
            widgets::text(c, ui, x, row, std::string(1, model::track_label(t)),
                          t == song.current_track ? gfx::kInkBright : gfx::kInkDim);
            widgets::value_field(c, ui, x + 2, row, 4, model::midi_dest_name(tr.dest), focused(f), zone(f));
            const bool ext = tr.dest != model::MidiDest::Internal;
            widgets::value_field(c, ui, x + 7, row, 10, ext ? fit(to_upper(tr.out_port.empty() ? "FIRST" : tr.out_port), 10) : "--",
                                 focused(f + 1), zone(f + 1));
            std::snprintf(buf, sizeof buf, "%2d", tr.out_channel + 1);
            widgets::value_field(c, ui, x + 18, row, 2, buf, focused(f + 2), zone(f + 2));
            if (tr.program < 0) {
                std::snprintf(buf, sizeof buf, " --");
            } else {
                std::snprintf(buf, sizeof buf, "%3d", tr.program + 1);
            }
            widgets::value_field(c, ui, x + 22, row, 3, buf, focused(f + 3), zone(f + 3));
        }
    }
    std::snprintf(buf, sizeof buf, "SENT %llu   WORST LATE %.2f MS   LOST %llu", static_cast<unsigned long long>(mi.out_sent),
                  mi.out_worst_late_ms, static_cast<unsigned long long>(mi.out_lost));
    widgets::small_text(c, ui, px(2), py(25) + 2, buf, gfx::kInkDim);
    widgets::small_text(c, ui, px(2), py(26) + 2,
                        "INT = INSTRUMENT, EXT = MIDI OUT.  FIRST = FIRST OPEN OUTPUT.  PROG AT PATTERN START",
                        gfx::kInkDim);
    widgets::small_text(c, ui, px(2), py(27) + 2,
                        "LEFT/RIGHT CHANGE, SHIFT+LEFT/RIGHT COLUMN, PAGE UP/DOWN: INPUTS.  PANIC: MOD+ESC",
                        gfx::kInkDim);
}

} // namespace phos::ui
