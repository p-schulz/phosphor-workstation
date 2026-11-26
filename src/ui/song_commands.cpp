// Song verbs handled by the UI itself (document edits, no host needed): TEMPO, SIG, SWING, TAP,
// METRO, MODE, QUANTIZE, TRACK. Transport and project verbs go to the host.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/text.h"
#include "io/snes.h"
#include "model/wavetable.h"
#include "ui/song_commands.h"

namespace phos::ui {

namespace {

std::optional<double> number(const std::string& s) {
    try {
        std::size_t used = 0;
        const double v = std::stod(s, &used);
        return used == s.size() ? std::optional<double>(v) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::string bpm_text(int bpm10) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.1f", bpm10 / 10.0);
    return buf;
}

std::optional<model::Grid> parse_grid(const std::string& s) {
    for (int g = 0; g <= static_cast<int>(model::Grid::ThirtySecondT); ++g) {
        if (iequals(s, model::grid_name(static_cast<model::Grid>(g)))) {
            return static_cast<model::Grid>(g);
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> run_song_command(Ui& ui, const Command& cmd, std::vector<double>& taps) {
    model::Workspace& ws = ui.app().workspace();
    model::Song song = ws.doc().song;
    model::Section& sec = song.sections[static_cast<std::size_t>(song.current_section)];
    const char section = static_cast<char>('A' + song.current_section);
    auto err = [&](std::string text) {
        ui.message(std::move(text), true);
        return std::optional<std::string>(std::string{});
    };

    if (cmd.verb == "TEMPO") {
        const auto v = cmd.args.empty() ? std::nullopt : number(cmd.args[0]);
        if (!v || *v < 20.0 || *v > 300.0) {
            return err("TEMPO 20.0-300.0");
        }
        sec.bpm10 = static_cast<int>(std::lround(*v * 10.0));
        ws.set_song(song, "TEMPO", ui.app().time_s());
        return "SECTION " + std::string(1, section) + " TEMPO " + bpm_text(sec.bpm10);
    }
    if (cmd.verb == "SIG") {
        const std::string a = cmd.args.empty() ? std::string{} : cmd.args[0];
        const auto slash = a.find('/');
        const auto num = slash == std::string::npos ? std::nullopt : number(a.substr(0, slash));
        const auto den = slash == std::string::npos ? std::nullopt : number(a.substr(slash + 1));
        const int d = den ? static_cast<int>(*den) : 0;
        if (!num || *num < 1 || *num > 16 || (d != 2 && d != 4 && d != 8 && d != 16)) {
            return err("SIG <1-16>/<2|4|8|16>");
        }
        sec.sig_num = static_cast<int>(*num);
        sec.sig_den = d;
        ws.set_song(song, "SIGNATURE");
        return "SECTION " + std::string(1, section) + " SIG " + std::to_string(sec.sig_num) + "/" + std::to_string(d);
    }
    if (cmd.verb == "SWING") {
        const auto v = cmd.args.empty() ? std::nullopt : number(cmd.args[0]);
        if (!v || *v < 50 || *v > 75) {
            return err("SWING 50-75 (TRACKS OPT IN ON PAGE R)");
        }
        song.swing = static_cast<int>(*v);
        if (cmd.args.size() > 1) {
            if (const auto g = parse_grid(cmd.args[1]); g && (*g == model::Grid::Eighth || *g == model::Grid::Sixteenth)) {
                song.swing_grid = *g;
            }
        }
        ws.set_song(song, "SWING", ui.app().time_s());
        return "SWING " + std::to_string(song.swing) + "% ON " + model::grid_name(song.swing_grid) + " PAIRS";
    }
    if (cmd.verb == "TAP") {
        const double now = ui.app().time_s();
        if (!taps.empty() && now - taps.back() > 2.0) {
            taps.clear(); // a pause starts a new count
        }
        taps.push_back(now);
        if (taps.size() > 5) {
            taps.erase(taps.begin());
        }
        if (taps.size() < 2) {
            return std::string("TAP: KEEP TAPPING");
        }
        const double interval = (taps.back() - taps.front()) / static_cast<double>(taps.size() - 1);
        const int bpm10 = std::clamp(static_cast<int>(std::lround(600.0 / interval)), 200, 3000);
        sec.bpm10 = bpm10;
        ws.set_song(song, "TAP TEMPO", now);
        return "TAP TEMPO " + bpm_text(bpm10);
    }
    if (cmd.verb == "METRO") {
        if (cmd.arg_is(0, "COUNT") && cmd.args.size() > 1) {
            const auto v = number(cmd.args[1]);
            if (!v || *v < 0 || *v > 2) {
                return err("METRO COUNT 0-2");
            }
            song.count_in_bars = static_cast<int>(*v);
            ws.set_song(song, "COUNT-IN");
            return "COUNT-IN " + std::to_string(song.count_in_bars) + " BAR" + (song.count_in_bars == 1 ? "" : "S");
        }
        if (cmd.arg_is(0, "OFF")) {
            song.metronome = model::MetronomeMode::Off;
        } else if (cmd.arg_is(0, "PLAY") || cmd.arg_is(0, "ON")) {
            song.metronome = model::MetronomeMode::Play;
        } else if (cmd.arg_is(0, "REC")) {
            song.metronome = model::MetronomeMode::Record;
        } else {
            return err("METRO OFF|PLAY|REC|COUNT <0-2>");
        }
        ws.set_song(song, "METRONOME");
        return "METRONOME " + to_upper(cmd.args[0]);
    }
    if (cmd.verb == "MODE") {
        if (cmd.arg_is(0, "PATTERN")) {
            song.play_mode = model::PlayMode::Pattern;
        } else if (cmd.arg_is(0, "SONG")) {
            song.play_mode = model::PlayMode::Song;
        } else {
            return err("MODE PATTERN|SONG");
        }
        ws.set_song(song, "PLAY MODE");
        return "MODE " + to_upper(cmd.args[0]);
    }
    if (cmd.verb == "QUANTIZE") {
        if (cmd.arg_is(0, "OFF")) {
            song.quantize_on_record = false;
        } else if (const auto g = cmd.args.empty() ? std::nullopt : parse_grid(cmd.args[0])) {
            song.quantize_on_record = true;
            song.record_quantize = *g;
        } else {
            return err("QUANTIZE OFF|1/4|1/8|1/16|1/32|1/64|1/8T|1/16T|1/32T");
        }
        ws.set_song(song, "QUANTIZE");
        return song.quantize_on_record ? std::string("RECORD QUANTIZE ") + model::grid_name(song.record_quantize)
                                       : std::string("RECORD QUANTIZE OFF");
    }
    if (cmd.verb == "KIND") {
        constexpr std::array<std::pair<const char*, model::InstrumentKind>, 7> kKinds = {{
            {"SAMPLER", model::InstrumentKind::Sampler}, {"WAVE", model::InstrumentKind::Wavetable},
            {"WAVETABLE", model::InstrumentKind::Wavetable}, {"SYNTH", model::InstrumentKind::Synth},
            {"DRUM", model::InstrumentKind::Drum}, {"GB", model::InstrumentKind::Gb},
            {"SNES", model::InstrumentKind::Snes}}};
        for (const auto& [name, kind] : kKinds) {
            if (cmd.arg_is(0, name)) {
                const model::Document& doc = ws.doc();
                const model::Instrument& old = doc.current_instrument();
                if (old.kind == kind) {
                    return "INSTRUMENT IS ALREADY " + std::string(model::kind_name(kind));
                }
                model::Instrument inst = model::make_instrument(kind, old.name == "EMPTY" ? model::kind_name(kind) : old.name);
                inst.sample = old.sample; // kept, in case it is switched back
                if (kind == model::InstrumentKind::Snes) {
                    if (!old.sample) {
                        return err("KIND SNES CONVERTS A SAMPLE: IMPORT ONE FIRST (PAGE 2)");
                    }
                    inst.sampler = old.sampler; // keep the loop for the BRR conversion
                    inst.snes.root_note = old.sampler.root_note;
                    inst.brr = io::encode_for_snes(*old.sample, inst.sampler, inst.snes);
                }
                ws.set_instrument(doc.current, inst, "KIND");
                return "INSTRUMENT " + std::to_string(doc.current + 1) + " IS NOW " + model::kind_name(kind);
            }
        }
        return err("KIND SAMPLER|WAVE|SYNTH|DRUM|GB|SNES");
    }
    if (cmd.verb == "COMPUTE") {
        const model::Document& doc = ws.doc();
        model::Instrument inst = doc.current_instrument();
        if (inst.kind != model::InstrumentKind::Wavetable || !inst.wavetable) {
            return err("COMPUTE: THE INSTRUMENT IS NOT A WAVETABLE (KIND WAVE)");
        }
        const bool normalize = !cmd.arg_is(0, "RAW");
        inst.wavetable = std::make_shared<const model::WavetableData>(model::wavetable::compute(*inst.wavetable, normalize));
        ws.set_instrument(doc.current, inst, "COMPUTE");
        return std::string(normalize ? "COMPUTED (NORMALIZED)" : "COMPUTED (RAW)");
    }
    if (cmd.verb == "SECTION") {
        const char c = cmd.args.empty() || cmd.args[0].size() != 1
                           ? '?'
                           : static_cast<char>(std::toupper(static_cast<unsigned char>(cmd.args[0][0])));
        if (c < 'A' || c > 'Z') {
            return err("SECTION A-Z");
        }
        ws.select_section(c - 'A');
        const auto& s = ws.doc().song.section();
        return "SECTION " + std::string(1, c) + ": PATTERN " + std::to_string(s.pattern + 1) + ", " + bpm_text(s.bpm10) +
               " BPM";
    }
    if (cmd.verb == "PATTERN") {
        const int count = static_cast<int>(song.patterns.size());
        char name[16];
        if (cmd.arg_is(0, "NEW") || cmd.arg_is(0, "COPY")) {
            if (count >= model::kMaxPatterns) {
                return err("PATTERN: 64 PATTERNS AT MOST");
            }
            model::Pattern p = cmd.arg_is(0, "COPY") ? song.pattern() : model::Pattern{};
            std::snprintf(name, sizeof name, "PATTERN %02d", count + 1);
            p.name = name;
            song.patterns.push_back(std::move(p));
            sec.pattern = count;
            ws.set_song(song, "PATTERN " + to_upper(cmd.args[0]));
            return std::string(name) + " FOR SECTION " + std::string(1, section);
        }
        const auto v = cmd.args.empty() ? std::nullopt : number(cmd.args[0]);
        if (!v || *v < 1 || *v > count) {
            return err("PATTERN 1-" + std::to_string(count) + "|NEW|COPY");
        }
        sec.pattern = static_cast<int>(*v) - 1;
        ws.set_song(song, "SECTION PATTERN");
        return "SECTION " + std::string(1, section) + " PLAYS PATTERN " + std::to_string(sec.pattern + 1);
    }
    if (cmd.verb == "TRACK") {
        const auto v = cmd.args.empty() ? std::nullopt : number(cmd.args[0]);
        int t = -1;
        if (v && *v >= 1 && *v <= 8) {
            t = static_cast<int>(*v) - 1;
        } else if (!cmd.args.empty() && cmd.args[0].size() == 1) {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd.args[0][0])));
            t = c >= 'A' && c <= 'H' ? 8 + (c - 'A') : -1;
        }
        if (t < 0) {
            return err("TRACK 1-8|A-H");
        }
        ws.select_track(t);
        const auto& tr = ws.doc().song.tracks[static_cast<std::size_t>(t)];
        return "TRACK " + std::string(1, model::track_label(t)) + " -> INSTRUMENT " + std::to_string(tr.instrument + 1);
    }
    return std::nullopt;
}

} // namespace phos::ui
