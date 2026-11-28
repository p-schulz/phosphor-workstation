// M7 verbs that only edit the document (so they work headless too): VAPOR, FX, SEND, RETURN,
// TRACKTYPE and the editing forms of LOOP. Launching loops, LOOP <n> LOAD and RESAMPLE need the
// engine or a worker and are handled by the host first.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/text.h"
#include "ui/song_commands.h"
#include "ui/ui.h"

namespace phos::ui {

namespace {

std::optional<double> number(const std::string& s) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

std::optional<int> slot_of(const std::string& a) {
    std::string s = to_upper(a);
    if (!s.empty() && s[0] == 'L') {
        s = s.substr(1);
    }
    return s.size() == 1 && s[0] >= '1' && s[0] <= '8' ? std::optional<int>(s[0] - '1') : std::nullopt;
}

// FX <1|2|M1|M2|RA|RB> <TYPE> [P1 P2 P3 P4 P5]: the slot it names, or nullptr.
model::FxSlot* fx_slot(model::Song& s, const std::string& which, std::string& label) {
    const std::string w = to_upper(which);
    auto& tr = s.tracks[static_cast<std::size_t>(s.current_track)];
    if (w == "1" || w == "2") {
        label = std::string("TRACK ") + model::track_label(s.current_track) + " INSERT " + w;
        return &tr.inserts[static_cast<std::size_t>(w[0] - '1')];
    }
    if (w == "M1" || w == "M2") {
        label = "MASTER INSERT " + w.substr(1);
        return &s.master_fx[static_cast<std::size_t>(w[1] - '1')];
    }
    if (w == "RA" || w == "RB") {
        label = std::string("RETURN ") + w[1];
        return &s.returns[static_cast<std::size_t>(w[1] - 'A')];
    }
    return nullptr;
}

} // namespace

std::optional<std::string> run_mix_command(Ui& ui, const Command& cmd) {
    model::Workspace& ws = ui.app().workspace();
    model::Song song = ws.doc().song;
    auto err = [&](std::string text) {
        ui.message(std::move(text), true);
        return std::optional<std::string>(std::string{});
    };

    if (cmd.verb == "VAPOR") {
        const auto v = cmd.args.empty() ? std::nullopt : number(cmd.args[0]);
        if (!v || *v < 0 || *v > model::kVaporMax) {
            return err("VAPOR <0-100>");
        }
        song.vapor = static_cast<int>(std::lround(*v));
        ws.set_song(song, "VAPOR", ui.app().time_s());
        return "VAPOR " + std::to_string(song.vapor);
    }
    if (cmd.verb == "TRACKTYPE") {
        auto& tr = song.tracks[static_cast<std::size_t>(song.current_track)];
        if (cmd.arg_is(0, "NOTES") || cmd.arg_is(0, "LOOPS")) {
            tr.type = cmd.arg_is(0, "LOOPS") ? model::TrackType::Loops : model::TrackType::Notes;
            ws.set_song(song, "TRACK TYPE");
            return std::string("TRACK ") + model::track_label(song.current_track) +
                   (tr.type == model::TrackType::Loops ? " LAUNCHES LOOPS (C4..G4 = SLOT 1..8)" : " PLAYS NOTES");
        }
        return err("TRACKTYPE NOTES|LOOPS");
    }
    if (cmd.verb == "SEND") {
        const auto v = cmd.args.size() < 2 ? std::nullopt : number(cmd.args[1]);
        if (!v || *v < 0 || *v > 100 || !(cmd.arg_is(0, "A") || cmd.arg_is(0, "B"))) {
            return err("SEND A|B <0-100>");
        }
        auto& tr = song.tracks[static_cast<std::size_t>(song.current_track)];
        (cmd.arg_is(0, "A") ? tr.send_a : tr.send_b) = static_cast<float>(*v / 100.0);
        ws.set_song(song, "SEND", ui.app().time_s());
        return std::string("TRACK ") + model::track_label(song.current_track) + " SEND " + to_upper(cmd.args[0]) +
               " " + std::to_string(static_cast<int>(std::lround(*v)));
    }
    if (cmd.verb == "RETURN") {
        const auto v = cmd.args.size() < 2 ? std::nullopt : number(cmd.args[1]);
        if (!v || *v < 0 || *v > 100 || !(cmd.arg_is(0, "A") || cmd.arg_is(0, "B"))) {
            return err("RETURN A|B <0-100>");
        }
        song.return_level[cmd.arg_is(0, "A") ? 0 : 1] = static_cast<float>(*v / 100.0);
        ws.set_song(song, "RETURN LEVEL", ui.app().time_s());
        return "RETURN " + to_upper(cmd.args[0]) + " " + std::to_string(static_cast<int>(std::lround(*v)));
    }
    if (cmd.verb == "FX") {
        std::string label;
        model::FxSlot* slot = cmd.args.empty() ? nullptr : fx_slot(song, cmd.args[0], label);
        model::FxType type{};
        if (!slot || cmd.args.size() < 2 || !model::fx_from_name(to_upper(cmd.args[1]), type)) {
            return err("FX 1|2|M1|M2|RA|RB OFF|CHORUS|TAPE|CRUSH|LOFI|PLATE|DELAY|DUCK|GATE [VALUES]");
        }
        *slot = model::make_fx(type);
        for (std::size_t k = 2; k < cmd.args.size() && k - 2 < slot->p.size(); ++k) {
            if (const auto v = number(cmd.args[k])) {
                slot->p[k - 2] = static_cast<float>(*v);
            }
        }
        if (label.rfind("RETURN", 0) == 0) {
            slot->p[model::kFxParams - 1] = 1.0f; // returns are all wet
        }
        model::clamp_fx(*slot);
        ws.set_song(song, "FX");
        return label + ": " + model::fx_name(type);
    }
    if (cmd.verb == "TRACKOUT") {
        // TRACKOUT INT|EXT|BOTH [CH <1-16>] [PROG <1-128>|OFF] [PORT <name>|FIRST]
        auto& tr = song.tracks[static_cast<std::size_t>(song.current_track)];
        const std::string usage = "TRACKOUT INT|EXT|BOTH [CH 1-16] [PROG 1-128|OFF] [PORT <NAME>|FIRST]";
        if (cmd.args.empty()) {
            return err(usage);
        }
        const std::string d = to_upper(cmd.args[0]);
        if (d == "EXT" || d == "EXTERNAL") {
            tr.dest = model::MidiDest::External;
        } else if (d == "BOTH") {
            tr.dest = model::MidiDest::Both;
        } else if (d == "INT" || d == "INTERNAL") {
            tr.dest = model::MidiDest::Internal;
        } else {
            return err(usage);
        }
        for (std::size_t i = 1; i + 1 < cmd.args.size(); i += 2) {
            const std::string key = to_upper(cmd.args[i]);
            const auto v = number(cmd.args[i + 1]);
            if (key == "CH" && v && *v >= 1 && *v <= 16) {
                tr.out_channel = static_cast<int>(*v) - 1;
            } else if (key == "PROG" && (cmd.arg_is(i + 1, "OFF") || (v && *v >= 1 && *v <= 128))) {
                tr.program = v ? static_cast<int>(*v) - 1 : -1;
            } else if (key == "PORT") {
                std::string name;
                for (std::size_t k = i + 1; k < cmd.args.size(); ++k) {
                    name += (k > i + 1 ? " " : "") + cmd.args[k];
                }
                tr.out_port = iequals(name, "FIRST") ? std::string{} : name;
                break;
            } else {
                return err(usage);
            }
        }
        ws.set_song(song, "TRACK MIDI OUT");
        return std::string("TRACK ") + model::track_label(song.current_track) + " -> " + model::midi_dest_name(tr.dest) +
               " CH " + std::to_string(tr.out_channel + 1) + (tr.program >= 0 ? " PROG " + std::to_string(tr.program + 1) : "") +
               (tr.dest == model::MidiDest::Internal ? "" : tr.out_port.empty() ? " (FIRST OUTPUT)" : " " + to_upper(tr.out_port));
    }
    if (cmd.verb == "MIDIOUT") {
        // MIDIOUT ON|OFF | THRU ON|OFF | OFFSET <ms> | COMP ON|OFF | PORT <n> ON|OFF | CLOCK <n> ON|OFF
        MidiOutConfig c = ui.app().midi_out_config();
        const std::string usage = "MIDIOUT ON|OFF|THRU ON|OFF|OFFSET <MS>|COMP ON|OFF|PORT <N> ON|OFF|CLOCK <N> ON|OFF";
        const std::string a = cmd.args.empty() ? std::string{} : to_upper(cmd.args[0]);
        const auto on_off = [&](std::size_t i) -> std::optional<bool> {
            if (cmd.arg_is(i, "ON")) {
                return true;
            }
            if (cmd.arg_is(i, "OFF")) {
                return false;
            }
            return std::nullopt;
        };
        if (a == "ON" || a == "OFF") {
            c.enabled = a == "ON";
        } else if ((a == "THRU" || a == "COMP") && on_off(1)) {
            (a == "THRU" ? c.thru : c.latency_comp) = *on_off(1);
        } else if (a == "OFFSET" && cmd.args.size() > 1 && number(cmd.args[1]) && std::fabs(*number(cmd.args[1])) <= 100) {
            c.offset_ms = static_cast<int>(std::lround(*number(cmd.args[1])));
        } else if ((a == "PORT" || a == "CLOCK") && cmd.args.size() > 2 && on_off(cmd.args.size() - 1)) {
            // The output by its number on Page 3 OUT, or by name ("MIDIOUT PORT MY SYNTH ON").
            const auto& outs = ui.app().midi().outputs;
            std::string name;
            for (std::size_t k = 1; k + 1 < cmd.args.size(); ++k) {
                name += (k > 1 ? " " : "") + cmd.args[k];
            }
            if (const auto n = number(name)) {
                if (*n < 1 || *n > static_cast<double>(outs.size())) {
                    return err("MIDIOUT " + a + " 1-" + std::to_string(outs.size()) + " ON|OFF");
                }
                name = outs[static_cast<std::size_t>(*n) - 1].name;
            } else {
                const auto it = std::find_if(outs.begin(), outs.end(), [&](const auto& o) { return iequals(o.name, name); });
                if (it == outs.end()) {
                    return err("NO MIDI OUTPUT " + to_upper(name));
                }
                name = it->name;
            }
            MidiOutputSetting* set = c.find(name);
            if (!set) {
                c.outputs.push_back(MidiOutputSetting{name});
                set = &c.outputs.back();
            }
            (a == "PORT" ? set->enabled : set->clock) = *on_off(cmd.args.size() - 1);
        } else {
            return err(usage);
        }
        ui.app().set_midi_out_config(c);
        char buf[96];
        std::snprintf(buf, sizeof buf, "MIDI OUT %s  THRU %s  OFFSET %+d MS  LATENCY COMP %s", c.enabled ? "ON" : "OFF",
                      c.thru ? "ON" : "OFF", c.offset_ms, c.latency_comp ? "ON" : "OFF");
        return std::string(buf);
    }
    if (cmd.verb != "LOOP") {
        return std::nullopt;
    }
    // LOOP <n> FROM <inst> | CLEAR | BARS <n> | SYNC VARI|STRETCH|SLICE | QUANT NOW|BEAT|BAR | TRACK <t>
    const auto n = cmd.args.empty() ? std::nullopt : slot_of(cmd.args[0]);
    if (!n || cmd.args.size() < 2) {
        return err("LOOP <1-8> PLAY|STOP|LOAD <PATH>|FROM <INST>|CLEAR|BARS <N>|SYNC <MODE>|QUANT <Q>");
    }
    model::LoopSlot& slot = song.loops[static_cast<std::size_t>(*n)];
    const std::string tag = "LOOP " + std::to_string(*n + 1);
    const std::string what = to_upper(cmd.args[1]);
    const std::string arg = cmd.args.size() > 2 ? to_upper(cmd.args[2]) : std::string{};
    if (what == "CLEAR") {
        const int track = slot.track;
        slot = model::LoopSlot{};
        slot.track = track;
        ws.set_song(song, "LOOP CLEAR");
        return tag + " CLEARED";
    }
    if (what == "FROM") {
        const auto i = number(arg);
        if (!i || *i < 1 || *i > model::kInstruments) {
            return err("LOOP <N> FROM <1-16>");
        }
        const model::Instrument& inst = ws.doc().instruments[static_cast<std::size_t>(*i - 1)];
        if (!inst.sample) {
            return err("INSTRUMENT " + arg + " HAS NO SAMPLE");
        }
        slot.sample = inst.sample;
        slot.name = inst.name;
        slot.bars = model::detect_bars(inst.sample->seconds(), song.section().bpm10 / 10.0);
        ws.set_song(song, "LOOP FROM");
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s: %s, %d BAR(S) = %.1f BPM", tag.c_str(), fit(slot.name, 12).c_str(), slot.bars,
                      slot.loop_bpm());
        return std::string(buf);
    }
    if (what == "BARS") {
        const auto v = number(arg);
        if (!v || *v < 1 || *v > 64) {
            return err("LOOP <N> BARS <1-64>");
        }
        slot.bars = static_cast<int>(*v);
        ws.set_song(song, "LOOP BARS");
        return tag + " BARS " + std::to_string(slot.bars);
    }
    if (what == "SYNC") {
        constexpr std::array<model::LoopSync, 3> kModes = {model::LoopSync::Varispeed, model::LoopSync::Stretch,
                                                          model::LoopSync::Slice};
        for (const auto m : kModes) {
            if (!arg.empty() && (istarts_with(model::loop_sync_name(m), arg) || istarts_with(arg, model::loop_sync_name(m)))) {
                slot.sync = m;
                ws.set_song(song, "LOOP SYNC");
                return tag + " SYNC " + model::loop_sync_name(m);
            }
        }
        return err("LOOP <N> SYNC VARI|STRETCH|SLICE");
    }
    if (what == "QUANT") {
        constexpr std::array<model::LaunchQuantize, 3> kQ = {model::LaunchQuantize::Immediate,
                                                            model::LaunchQuantize::Beat, model::LaunchQuantize::Bar};
        for (const auto q : kQ) {
            if (arg == model::launch_quantize_name(q)) {
                slot.quantize = q;
                ws.set_song(song, "LOOP QUANT");
                return tag + " QUANT " + arg;
            }
        }
        return err("LOOP <N> QUANT NOW|BEAT|BAR");
    }
    // PLAY / STOP / LOAD reach this point only without a host (headless).
    return err(tag + " " + what + ": NEEDS THE AUDIO ENGINE");
}

} // namespace phos::ui
