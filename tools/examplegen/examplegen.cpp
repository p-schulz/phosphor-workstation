// examplegen: writes PHOSPHOR's example content into examples/ (no third-party audio; everything
// is synthesised or rendered offline):
//   examples/library/*.phinst      an instrument library (Game Boy, SNES, vaporwave, shapes, GM)
//   examples/projects/*.phosphor   GB ADVENTURE, SNES FIELD, VAPOR PLAZA, WAVEFORMS: built by
//                                  loading instruments back from that library, like a user would
//   examples/midi/GB ADVENTURE.mid the GB song as MIDI, for "IMPORT" with the library
//
//   examplegen <examples dir>        (CMake target `examples`)

#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "content/presets.h"
#include "engine/offline.h"
#include "io/instrument_io.h"
#include "io/project.h"
#include "io/smf.h"
#include "io/snes.h"
#include "model/chip.h"
#include "model/wavetable.h"

namespace fs = std::filesystem;
using namespace phos;
using namespace phos::model;
using content::presets::chord;
using content::presets::note;

namespace {

constexpr std::uint32_t kRate = 32000; // the SNES rate: good enough, half the size of 48 kHz files

fs::path g_library;

[[noreturn]] void die(const std::string& what) {
    std::fprintf(stderr, "examplegen: %s\n", what.c_str());
    std::exit(1);
}

// --- Library ---------------------------------------------------------------------------------

void put(const Instrument& inst) {
    std::string error;
    if (!io::save_instrument(io::instrument_path(g_library, inst.name), inst, &error)) {
        die(error);
    }
    std::printf("  library: %s (%s)\n", inst.name.c_str(), kind_name(inst.kind));
}

// What the projects do: take an instrument from the library by name.
Instrument take(const std::string& name) {
    std::string error;
    auto inst = io::load_instrument(io::instrument_path(g_library, name), kRate, &error);
    if (!inst) {
        die("library: " + name + ": " + error);
    }
    return std::move(*inst);
}

Instrument named(Instrument i, const char* name) {
    i.name = name;
    return i;
}

// A noise-channel drum. `shift` (semitones, via the arp macro) moves the GM drum note it is played
// with (36 kick, 38 snare, 42 hat) to the noise pitch that sounds right.
Instrument gb_drum(const char* name, int pace, bool seven_bit, int volume, int shift) {
    Instrument i = content::presets::gb_noise(name, seven_bit, pace);
    i.gb.env_volume = volume;
    if (shift != 0) {
        i.gb.macros[kMacroArp] = GbMacro{{static_cast<std::int8_t>(shift)}, 0, -1};
    }
    return i;
}

// A raw oscillator shape; `level_db` evens out the loudness (measured RMS) across the shapes.
Instrument shape(const char* name, OscWave wave, float level_db, float pulse_width = 0.5f, int unison = 1) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = wave;
    i.synth.osc2 = OscWave::Off;
    i.synth.pulse_width = pulse_width;
    i.synth.unison = unison;
    i.synth.unison_spread = 18.0f;
    i.sampler.filter = FilterType::Off; // the raw shape
    i.sampler.amp = Adsr{5.0f, 200.0f, 0.85f, 250.0f};
    i.sampler.volume_db = level_db;
    return i;
}

void build_library() {
    using namespace content::presets;
    // Game Boy.
    put(gb_pulse("GB PULSE 12", 0));
    put(gb_pulse("GB PULSE 25", 1));
    put(gb_pulse("GB PULSE 50", 2, false));
    put(gb_pulse("GB PULSE 75", 3, false));
    put(named(make_instrument(InstrumentKind::Gb, "GB LEAD"), "GB LEAD"));
    Instrument arp = gb_pulse("GB ARP", 2, false);
    arp.gb.macros[kMacroArp] = GbMacro{{0, 0, 4, 4, 7, 7}, 0, -1}; // major chord, 2 ticks a note
    arp.gb.macros[kMacroVolume] = GbMacro{{11}, -1, -1};
    put(arp);
    put(gb_wave("GB WAVE TRI", 0));
    put(gb_wave("GB WAVE SAW", 1));
    put(gb_wave("GB WAVE ORGAN", 2));
    put(gb_drum("GB NOISE KICK", 1, false, 15, 0));
    put(gb_drum("GB NOISE SNARE", 2, false, 13, 22));
    put(gb_drum("GB NOISE HAT", 1, true, 9, 42));
    // SNES: synth patches rendered and converted to BRR.
    Instrument strings = to_snes(pad("STR"), "SNES STRINGS", kRate, true);
    strings.snes.attack = 8;
    strings.snes.sustain_level = 6;
    put(strings);
    Instrument brass_s = to_snes(brass("BR"), "SNES BRASS", kRate, true);
    brass_s.snes.attack = 12;
    put(brass_s);
    Instrument flute = to_snes(reed("FL"), "SNES FLUTE", kRate, true);
    flute.snes.attack = 11;
    put(flute);
    put(to_snes(synth_bass("BS", 900.0f, 1.5f), "SNES BASS", kRate, true));
    // One-shots at a 16 kHz BRR rate (console-typical), so the song fits the 64 KB ARAM.
    put(to_snes(pluck("PL"), "SNES HARP", kRate, false, 0.8, 69, 69, 16000));
    put(to_snes(drum_kit("K"), "SNES KICK", kRate, false, 0.4, 36, 60, 16000));
    put(to_snes(drum_kit("K"), "SNES SNARE", kRate, false, 0.4, 38, 60, 16000));
    put(to_snes(drum_kit("K"), "SNES HAT", kRate, false, 0.2, 42, 60, 16000));
    // Vaporwave.
    put(named(electric_piano(), "E.PIANO"));
    put(named(pad("VAPOR PAD"), "VAPOR PAD"));
    put(named(synth_bass("SYNTH BASS", 600.0f, 0.6f), "SYNTH BASS"));
    put(named(drum_kit("DRUM KIT"), "DRUM KIT"));
    put(named(bell("GLASS BELL"), "GLASS BELL"));
    // Shapes (WAVEFORMS).
    put(shape("SHAPE SINE", OscWave::Sine, -4.0f));
    put(shape("SHAPE TRIANGLE", OscWave::Triangle, -2.4f));
    put(shape("SHAPE SAW", OscWave::Saw, -2.4f));
    put(shape("SHAPE SQUARE", OscWave::Pulse, -7.2f, 0.5f));
    put(shape("SHAPE PULSE 25", OscWave::Pulse, -7.2f, 0.25f));
    put(shape("SHAPE PULSE 10", OscWave::Pulse, -7.2f, 0.10f));
    put(shape("SHAPE NOISE", OscWave::Noise, -2.4f));
    put(shape("SHAPE SUPERSAW", OscWave::Saw, 0.0f, 0.5f, 4));
    Instrument wt = make_instrument(InstrumentKind::Wavetable, "SHAPE WAVETABLE");
    put(wt);
    put(to_snes(shape("S", OscWave::Saw, 0.0f), "SHAPE SNES SAW", kRate, true));
    // General MIDI family names: IMPORT song.mid picks these for matching tracks.
    put(named(take("SYNTH BASS"), "BASS"));
    put(named(take("SNES STRINGS"), "STRINGS"));
    put(named(take("GB LEAD"), "SYNTH LEAD"));
    put(named(take("E.PIANO"), "PIANO"));
    put(named(take("DRUM KIT"), "DRUMS"));
}

// --- Song helpers ----------------------------------------------------------------------------

// A melody: (note, beats) in sequence from `start`; note -1 = rest.
void melody(Pattern& p, int track, double start, std::initializer_list<std::pair<int, double>> notes, int vel = 100) {
    double at = start;
    for (const auto& [n, beats] : notes) {
        if (n >= 0) {
            note(p, track, at, beats * 0.9, n, vel);
        }
        at += beats;
    }
}

Pattern bars(const char* name, int count) {
    Pattern p;
    p.name = name;
    p.steps = 16 * count;
    return p;
}

// Track `t` plays instrument `t`, loaded from the library and named after its role in the song.
void track(Song& s, int t, const char* name, Document& d, const std::string& library_name) {
    d.instruments[static_cast<std::size_t>(t)] = take(library_name);
    d.instruments[static_cast<std::size_t>(t)].name = name;
    s.tracks[static_cast<std::size_t>(t)].name = name;
    s.tracks[static_cast<std::size_t>(t)].instrument = t;
}

void save(const Document& d, const fs::path& dir, const char* name) {
    std::string error;
    const auto bundle = dir / (std::string(name) + ".phosphor");
    fs::remove_all(bundle);
    if (!io::save_project(bundle, d, &error)) {
        die(error);
    }
    std::printf("  project: %s (ARAM %.1f KB)\n", bundle.filename().string().c_str(), io::aram_bytes(d) / 1024.0);
}

// --- GB ADVENTURE ----------------------------------------------------------------------------
// A minor, 140 BPM. Intro (arpeggios), verse (lead), chorus (lead + harmony), bridge (bass and
// noise only), as a chiptune would arrange it.

Document gb_adventure() {
    Document d;
    Song& s = d.song;
    track(s, 0, "LEAD", d, "GB PULSE 25");
    track(s, 1, "HARMONY", d, "GB PULSE 12");
    track(s, 2, "ARP", d, "GB ARP");
    track(s, 3, "BASS", d, "GB WAVE TRI");
    track(s, 4, "KICK", d, "GB NOISE KICK");
    track(s, 5, "SNARE", d, "GB NOISE SNARE");
    track(s, 6, "HAT", d, "GB NOISE HAT");
    d.instruments[0].gb.vibrato_cents = 20;
    d.instruments[0].gb.vibrato_delay = 16;
    const int roots[4] = {45, 41, 48, 43}; // Am F C G
    const auto bass = [&](Pattern& p) {
        for (int bar = 0; bar < 4; ++bar) {
            for (int e = 0; e < 8; ++e) {
                note(p, 3, bar * 4 + e * 0.5, 0.45, roots[bar] - 12 + (e % 2 ? 12 : 0), 100);
            }
        }
    };
    const auto drums = [&](Pattern& p, bool busy) {
        for (int bar = 0; bar < 4; ++bar) {
            const double b = bar * 4.0;
            note(p, 4, b, 0.25, 36, 120);
            note(p, 4, b + 2.0, 0.25, 36, 120);
            if (busy) {
                note(p, 4, b + 2.5, 0.25, 36, 100);
            }
            note(p, 5, b + 1.0, 0.25, 38, 110);
            note(p, 5, b + 3.0, 0.25, 38, 110);
            for (int e = 0; e < 8; ++e) {
                note(p, 6, b + e * 0.5, 0.2, 42, e % 2 ? 70 : 100);
            }
        }
    };
    const auto arps = [&](Pattern& p) {
        for (int bar = 0; bar < 4; ++bar) {
            for (int beat = 0; beat < 4; ++beat) {
                note(p, 2, bar * 4 + beat, 0.9, roots[bar] + 12, 90); // the arp macro plays the chord
            }
        }
    };
    Pattern intro = bars("INTRO", 4);
    arps(intro);
    bass(intro);
    Pattern verse = bars("VERSE", 4);
    melody(verse, 0, 0, {{69, 1}, {72, 0.5}, {71, 0.5}, {69, 1}, {64, 1}, {65, 1.5}, {64, 0.5}, {62, 1}, {60, 1},
                         {64, 1}, {67, 0.5}, {69, 0.5}, {67, 1}, {64, 1}, {67, 1.5}, {65, 0.5}, {64, 1}, {62, 1}});
    bass(verse);
    drums(verse, false);
    Pattern chorus = bars("CHORUS", 4);
    melody(chorus, 0, 0, {{76, 1.5}, {74, 0.5}, {72, 1}, {74, 1}, {77, 1.5}, {76, 0.5}, {74, 2},
                          {79, 1.5}, {77, 0.5}, {76, 1}, {74, 1}, {71, 2}, {-1, 1}, {74, 1}});
    melody(chorus, 1, 0, {{72, 1.5}, {71, 0.5}, {69, 1}, {71, 1}, {74, 1.5}, {72, 0.5}, {69, 2},
                          {76, 1.5}, {74, 0.5}, {72, 1}, {71, 1}, {67, 2}, {-1, 1}, {71, 1}}, 80);
    arps(chorus);
    bass(chorus);
    drums(chorus, true);
    Pattern bridge = bars("BRIDGE", 4);
    bass(bridge);
    for (int bar = 0; bar < 4; ++bar) {
        note(bridge, 4, bar * 4.0, 0.25, 36, 120);
        note(bridge, 5, bar * 4.0 + 3.5, 0.25, 38, 90);
    }
    melody(bridge, 1, 8, {{64, 2}, {65, 2}, {67, 3}, {-1, 1}}, 70);
    s.patterns = {intro, verse, chorus, bridge};
    for (int k = 0; k < 4; ++k) {
        s.sections[static_cast<std::size_t>(k)] = Section{k, 1, 1400, 4, 4};
    }
    s.order = {0, 1, 2, 1, 2, 3, 2};
    s.play_mode = PlayMode::Song;
    s.metronome = MetronomeMode::Off;
    s.tracks[1].volume_db = -4.0f;
    s.tracks[2].volume_db = -8.0f;
    s.tracks[2].pan = -0.4f;
    s.tracks[1].pan = 0.3f;
    s.tracks[6].volume_db = -6.0f;
    // General MIDI for the exported .mid: leads 81 / 82 (square / saw lead), bass 39 (synth bass),
    // drums on channel 10 with GM notes. IMPORT picks SYNTH LEAD, BASS and DRUMS from a library.
    for (const auto& [t, prog] : {std::pair{0, 80}, std::pair{1, 80}, std::pair{2, 81}, std::pair{3, 38}}) {
        s.tracks[static_cast<std::size_t>(t)].program = prog;
    }
    for (int t = 4; t <= 6; ++t) {
        s.tracks[static_cast<std::size_t>(t)].out_channel = 9;
    }
    s.master_db = -4.0f;
    return d;
}

// --- SNES FIELD ------------------------------------------------------------------------------
// D major, 96 BPM: a field-theme of an RPG. Strings bed, flute theme, brass answer, harp runs,
// bass and drums from BRR samples, S-DSP echo (low-pass FIR) on everything.

Document snes_field() {
    Document d;
    Song& s = d.song;
    track(s, 0, "FLUTE", d, "SNES FLUTE");
    track(s, 1, "BRASS", d, "SNES BRASS");
    track(s, 2, "STRINGS", d, "SNES STRINGS");
    track(s, 3, "HARP", d, "SNES HARP");
    track(s, 4, "BASS", d, "SNES BASS");
    track(s, 5, "KICK", d, "SNES KICK");
    track(s, 6, "SNARE", d, "SNES SNARE");
    track(s, 7, "HAT", d, "SNES HAT");
    d.instruments[0].snes.vol_l = 90;
    d.instruments[0].snes.vol_r = 110;
    d.instruments[3].snes.vol_l = 110;
    d.instruments[3].snes.vol_r = 70;
    const std::array<std::initializer_list<int>, 4> chords = {{{50, 57, 62, 66}, {47, 54, 59, 62}, {43, 50, 55, 59}, {45, 52, 57, 61}}};
    const int roots[4] = {38, 35, 31, 33}; // D Bm G A
    const auto bed = [&](Pattern& p, bool full) {
        for (int bar = 0; bar < 4; ++bar) {
            chord(p, 2, bar * 4.0, 4.0, chords[static_cast<std::size_t>(bar)], full ? 80 : 60);
            if (full) {
                note(p, 4, bar * 4.0, 1.5, roots[bar], 110);
                note(p, 4, bar * 4.0 + 1.5, 0.5, roots[bar] + 7, 90);
                note(p, 4, bar * 4.0 + 2, 2.0, roots[bar] + 12, 100);
                note(p, 5, bar * 4.0, 0.5, 60, 120);
                note(p, 5, bar * 4.0 + 2.5, 0.5, 60, 100);
                note(p, 6, bar * 4.0 + 1, 0.5, 60, 110);
                note(p, 6, bar * 4.0 + 3, 0.5, 60, 110);
                for (int e = 0; e < 8; ++e) {
                    note(p, 7, bar * 4.0 + e * 0.5, 0.25, 60, e % 2 ? 60 : 90);
                }
            }
        }
    };
    Pattern intro = bars("DAWN", 4);
    bed(intro, false);
    for (int bar = 0; bar < 4; ++bar) { // harp arpeggios
        const auto& c = chords[static_cast<std::size_t>(bar)];
        int k = 0;
        for (const int n : c) {
            note(intro, 3, bar * 4.0 + k * 0.5, 0.5, n + 12, 90);
            note(intro, 3, bar * 4.0 + 2 + k * 0.5, 0.5, n + 24, 70);
            ++k;
        }
    }
    Pattern theme = bars("THEME", 4);
    bed(theme, true);
    melody(theme, 0, 0, {{74, 1.5}, {76, 0.5}, {78, 1}, {81, 1}, {79, 1.5}, {78, 0.5}, {74, 2},
                         {71, 1}, {74, 1}, {79, 1.5}, {78, 0.5}, {76, 3}, {-1, 1}});
    Pattern answer = bars("ANSWER", 4);
    bed(answer, true);
    melody(answer, 1, 0, {{62, 0.5}, {66, 0.5}, {69, 2}, {-1, 1}, {66, 0.5}, {69, 0.5}, {74, 2}, {-1, 1},
                          {67, 1}, {71, 1}, {74, 2}, {69, 1}, {73, 1}, {76, 2}});
    melody(answer, 0, 0, {{-1, 3}, {78, 1}, {-1, 3}, {81, 1}, {-1, 4}, {-1, 2}, {79, 1}, {78, 1}}, 80);
    s.patterns = {intro, theme, answer};
    for (int k = 0; k < 3; ++k) {
        s.sections[static_cast<std::size_t>(k)] = Section{k, 1, 960, 4, 4};
    }
    s.order = {0, 1, 2, 1, 2};
    s.play_mode = PlayMode::Song;
    s.metronome = MetronomeMode::Off;
    s.snes_echo.delay = 6;
    s.snes_echo.feedback = 60;
    s.snes_echo.vol_l = 45;
    s.snes_echo.vol_r = 45;
    s.snes_echo.fir = kFirPresets[1];
    s.strict_snes = true; // eight voices, like the console
    s.master_db = -11.0f;
    return d;
}

// --- VAPOR PLAZA -----------------------------------------------------------------------------
// 72 BPM. Source: Gmaj7 / Em9 e-piano and bass at 96 BPM, resampled into two loop slots (VARISPEED
// for the intro and outro, SLICE for the groove); drums duck the groove loop; pad on the plate.

Document vapor_plaza() {
    Document d;
    Song& s = d.song;
    track(s, 0, "E.PIANO", d, "E.PIANO");
    track(s, 1, "PAD", d, "VAPOR PAD");
    track(s, 2, "BASS", d, "SYNTH BASS");
    track(s, 3, "DRUMS", d, "DRUM KIT");
    track(s, 5, "BELL", d, "GLASS BELL");
    s.tracks[4].name = "LOOPS";
    s.tracks[4].type = TrackType::Loops;
    // Source at 96 BPM (section D, pattern 3), resampled.
    Pattern src = bars("SOURCE", 2);
    chord(src, 0, 0, 1.5, {55, 59, 62, 66}, 95);
    chord(src, 0, 1.5, 1.0, {55, 59, 62, 66}, 75);
    chord(src, 0, 3, 1.0, {55, 59, 62, 66}, 80);
    chord(src, 0, 4, 1.5, {52, 55, 59, 62, 66}, 95);
    chord(src, 0, 5.5, 1.0, {52, 55, 59, 62, 66}, 75);
    chord(src, 0, 7, 1.0, {52, 55, 59, 62, 66}, 80);
    melody(src, 2, 0, {{43, 1.5}, {43, 1}, {50, 1.5}, {40, 1.5}, {40, 1}, {47, 1.5}});
    Pattern intro = bars("ARRIVAL", 2);
    note(intro, 4, 0, 8, kLoopBaseNote, 100);     // slot 1, varispeed
    chord(intro, 1, 0, 8, {48, 55, 59, 64}, 60);  // pad (the loop sounds a fifth down: C / Am)
    Pattern groove = bars("ESCALATOR", 2);
    note(groove, 4, 0, 8, kLoopBaseNote + 1, 100); // slot 2, slices
    for (int beat = 0; beat < 8; ++beat) {
        note(groove, 3, beat, 0.5, 36, beat % 2 ? 90 : 115);
        if (beat % 2) {
            note(groove, 3, beat, 0.5, 38, 70);
        }
        note(groove, 3, beat + 0.5, 0.25, 42, 50);
    }
    melody(groove, 5, 0, {{79, 1}, {76, 1}, {72, 2}, {-1, 1}, {74, 1}, {71, 2}}, 70);
    Pattern outro = bars("CLOSING TIME", 2);
    note(outro, 4, 0, 8, kLoopBaseNote, 100);
    chord(outro, 1, 0, 8, {45, 52, 55, 60}, 55);
    melody(outro, 5, 4, {{76, 2}, {72, 2}}, 50);
    s.patterns = {intro, groove, outro, src};
    s.sections[0] = Section{0, 2, 720, 4, 4};
    s.sections[1] = Section{1, 4, 720, 4, 4};
    s.sections[2] = Section{2, 2, 720, 4, 4};
    s.sections[3] = Section{3, 1, 960, 4, 4};
    s.order = {0, 1, 2};
    s.play_mode = PlayMode::Song;
    s.metronome = MetronomeMode::Off;
    engine::OfflineRender opt;
    opt.section = 3;
    opt.seamless = true;
    opt.master = false;
    opt.sample_rate = kRate;
    opt.name = "PLAZA LOOP";
    const SampleRef loop = engine::render_section(d, opt);
    for (int k = 0; k < 2; ++k) {
        LoopSlot& l = s.loops[static_cast<std::size_t>(k)];
        l.sample = loop;
        l.name = k == 0 ? "PLAZA SLOW" : "PLAZA CHOP";
        l.bars = 2;
        l.sync = k == 0 ? LoopSync::Varispeed : LoopSync::Slice;
        l.slices = 16;
        l.track = 8 + k;
    }
    s.tracks[8].name = "LOOP SLOW";
    s.tracks[8].send_a = 0.4f;
    s.tracks[8].inserts[0] = make_fx(FxType::Tape);
    s.tracks[9].name = "LOOP CHOP";
    s.tracks[9].inserts[0] = make_fx(FxType::Duck);
    s.tracks[9].inserts[0].p = {3.0f, 0.7f, 160.0f, -26.0f, 0.0f};
    s.tracks[9].inserts[1] = make_fx(FxType::Chorus);
    s.tracks[1].send_a = 0.7f;
    s.tracks[1].volume_db = -5.0f;
    s.tracks[5].send_a = 0.5f;
    s.tracks[5].send_b = 0.3f;
    s.tracks[5].volume_db = -6.0f;
    s.tracks[9].volume_db = 5.0f;
    s.returns[0].p = {0.9f, 0.45f, 40.0f, 1.0f, 1.0f};
    s.vapor = 50;
    s.master_db = -2.0f;
    return d;
}

// --- WAVEFORMS -------------------------------------------------------------------------------
// One section per shape: the same two bars (an arpeggio, then a held note) on that shape's track,
// so stepping through the song order (or the sections) compares them one after another.

Document waveforms() {
    Document d;
    Song& s = d.song;
    const std::array<std::pair<const char*, const char*>, 16> shapes = {{
        // Labels fit the 8-character track names on Page R.
        {"SINE", "SHAPE SINE"},
        {"TRIANGLE", "SHAPE TRIANGLE"},
        {"SAW", "SHAPE SAW"},
        {"SQUARE", "SHAPE SQUARE"},
        {"PULSE 25", "SHAPE PULSE 25"},
        {"PULSE 10", "SHAPE PULSE 10"},
        {"SUPERSAW", "SHAPE SUPERSAW"},
        {"NOISE", "SHAPE NOISE"},
        {"GB 12.5%", "GB PULSE 12"},
        {"GB 25%", "GB PULSE 25"},
        {"GB 50%", "GB PULSE 50"},
        {"GB TRI 4", "GB WAVE TRI"},
        {"GB SAW 4", "GB WAVE SAW"},
        {"GB ORGAN", "GB WAVE ORGAN"},
        {"WAVETBL", "SHAPE WAVETABLE"},
        {"SNES SAW", "SHAPE SNES SAW"},
    }};
    s.patterns.clear();
    for (int k = 0; k < 16; ++k) {
        const auto& [label, lib] = shapes[static_cast<std::size_t>(k)];
        track(s, k, label, d, lib);
        Pattern p = bars(label, 2);
        melody(p, k, 0, {{48, 0.5}, {52, 0.5}, {55, 0.5}, {60, 0.5}, {64, 0.5}, {67, 0.5}, {72, 0.5}, {67, 0.5}});
        note(p, k, 4, 3.5, k == 7 ? 72 : 60, 100); // noise: higher is brighter
        s.patterns.push_back(p);
        s.sections[static_cast<std::size_t>(k)] = Section{k, 1, 1000, 4, 4};
    }
    // Track faders even out the chip and table voices (their level is the chip's own).
    const std::array<float, 16> trim = {0, 0, 0, 0, 0, 0, 0, 0, 0.8f, -1.6f, -2.9f, -2.3f, -2.3f, -3.0f, -5.2f, -8.3f};
    for (int k = 0; k < 16; ++k) {
        s.tracks[static_cast<std::size_t>(k)].volume_db = trim[static_cast<std::size_t>(k)];
    }
    s.order.clear();
    for (int k = 0; k < 16; ++k) {
        s.order.push_back(k);
    }
    s.play_mode = PlayMode::Song;
    s.metronome = MetronomeMode::Off;
    s.master_db = 0.0f;
    return d;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: examplegen <examples dir>\n");
        return 2;
    }
    const fs::path root = argv[1];
    g_library = root / "library";
    fs::remove_all(g_library);
    fs::create_directories(g_library);
    std::printf("examplegen: library\n");
    build_library();
    std::printf("examplegen: projects\n");
    const fs::path projects = root / "projects";
    fs::create_directories(projects);
    const Document gb = gb_adventure();
    save(gb, projects, "GB ADVENTURE");
    save(snes_field(), projects, "SNES FIELD");
    save(vapor_plaza(), projects, "VAPOR PLAZA");
    save(waveforms(), projects, "WAVEFORMS");
    fs::create_directories(root / "midi");
    std::string error;
    if (!io::write_smf(root / "midi" / "GB ADVENTURE.mid", gb, "GB ADVENTURE", &error)) {
        die(error);
    }
    std::printf("  midi: GB ADVENTURE.mid\n");
    return 0;
}
