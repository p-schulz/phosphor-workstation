// M8: offline render and export, SMF, automation lanes, MIDI out (PLAN.md §7.7, §7.9).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(PHOS_HAVE_LIBREMIDI)
#include <libremidi/reader.hpp>
#endif

#include "content/templates.h"
#include "engine/engine.h"
#include "engine/offline.h"
#include "engine/snapshot_builder.h"
#include "io/project.h"
#include "io/smf.h"
#include "io/wav.h"
#include "seq/timing.h"

using namespace phos;

namespace {

constexpr std::uint32_t kRate = 48000;

model::Document fixture(const char* name) {
    std::string error;
    auto doc = io::load_project(std::filesystem::path(PHOS_FIXTURE_DIR) / name, kRate, &error);
    INFO(error);
    REQUIRE(doc);
    return std::move(*doc);
}

bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

io::AudioBuffer interleave(const engine::Render& r) {
    io::AudioBuffer b;
    b.sample_rate = kRate;
    b.channels = 2;
    b.samples.resize(r.left.size() * 2);
    for (std::size_t i = 0; i < r.left.size(); ++i) {
        b.samples[2 * i] = r.left[i];
        b.samples[2 * i + 1] = r.right[i];
    }
    return b;
}

} // namespace

TEST_CASE("Two offline renders of the same project are bit-identical", "[m8][render][ac]") {
    // PLAN.md M8 AC. Projects with samples, synths, drums, chips, loops (all sync modes), effects,
    // ducking and VAPOR; the song order in SONG mode.
    std::vector<model::Document> docs = {fixture("demo.phosphor"), fixture("chip.phosphor"), fixture("vapor.phosphor")};
    auto funk = content::make_template("FUTURE-FUNK", kRate);
    REQUIRE(funk);
    funk->song.play_mode = model::PlayMode::Song;
    funk->song.order = {0, 0, 1};
    funk->song.loops[1] = funk->song.loops[0];
    funk->song.loops[1].sync = model::LoopSync::Stretch;
    docs.push_back(*funk);
    for (std::size_t k = 0; k < docs.size(); ++k) {
        CAPTURE(k);
        engine::SongRender opt;
        opt.tail_s = 1.0;
        const engine::Render a = engine::render_song(docs[k], opt);
        const engine::Render b = engine::render_song(docs[k], opt);
        REQUIRE(a.left.size() == engine::song_frames(docs[k].song, kRate) + kRate);
        CHECK(same_bits(a.left, b.left));
        CHECK(same_bits(a.right, b.right));
        CHECK(io::encode_wav(interleave(a), io::WavFormat::Pcm24) == io::encode_wav(interleave(b), io::WavFormat::Pcm24));
        float peak = 0.0f;
        for (const float x : a.left) {
            peak = std::max(peak, std::fabs(x));
        }
        CHECK(peak > 0.01f); // it is not silence
    }
}

TEST_CASE("Song length follows the order, repeats and tempos; stems solo each sounding track", "[m8][render]") {
    model::Document doc;
    model::Song& s = doc.song;
    s.patterns.resize(2);
    s.patterns[1].steps = 32; // two bars
    s.sections[0] = model::Section{0, 2, 1200, 4, 4}; // 2 x 1 bar at 120 = 4 s
    s.sections[1] = model::Section{1, 1, 600, 4, 4};  // 2 bars at 60 = 8 s
    s.order = {0, 1};
    s.play_mode = model::PlayMode::Song;
    CHECK(engine::song_frames(s, kRate) == 12 * kRate);
    s.play_mode = model::PlayMode::Pattern;
    s.current_section = 1;
    CHECK(engine::song_frames(s, kRate) == 8 * kRate);

    s.play_mode = model::PlayMode::Song;
    doc.instruments[0] = model::make_instrument(model::InstrumentKind::Synth, "A");
    doc.instruments[1] = model::make_instrument(model::InstrumentKind::Drum, "B");
    model::insert_note(s.patterns[0].lanes[0], model::Note{0, 960, 60, 100});
    model::insert_note(s.patterns[1].lanes[1], model::Note{0, 240, 36, 100});
    s.tracks[1].instrument = 1;
    s.tracks[5].mute = true;
    model::insert_note(s.patterns[0].lanes[5], model::Note{0, 960, 64, 100});
    CHECK(engine::stem_tracks(s) == std::vector<int>{0, 1}); // track 6 is muted
    s.play_mode = model::PlayMode::Pattern;
    s.current_section = 0;
    CHECK(engine::stem_tracks(s) == std::vector<int>{0}); // section B (track 2) does not play
    s.play_mode = model::PlayMode::Song;
    engine::SongRender opt;
    opt.stems = true;
    opt.tail_s = 0.5;
    const engine::Render r = engine::render_song(doc, opt);
    REQUIRE(r.stems.size() == 2);
    // Track 1 sounds in section A only (0..4 s), track 2 in section B only (4..12 s).
    const auto energy = [](const std::vector<float>& v, std::size_t from, std::size_t to) {
        double e = 0.0;
        for (std::size_t i = from; i < to; ++i) {
            e += static_cast<double>(v[i]) * v[i];
        }
        return e;
    };
    CHECK(energy(r.stems[0].left, 0, kRate) > 1.0);
    CHECK(energy(r.stems[0].left, 5 * kRate, 12 * kRate) < 1e-6);
    CHECK(energy(r.stems[1].left, 0, 4 * kRate) < 1e-6);
    CHECK(energy(r.stems[1].left, 4 * kRate, 5 * kRate) > 0.01);
}

// ---------------------------------------------------------------------------------------------
// Standard MIDI files
// ---------------------------------------------------------------------------------------------


namespace {

// Two sections: A (4/4, 120 BPM, 1 bar, repeated twice) and B (3/4, 90 BPM, 1 bar), SONG mode.
model::Document smf_doc() {
    model::Document d;
    model::Song& s = d.song;
    s.patterns.resize(2);
    s.patterns[1].steps = 12; // one 3/4 bar of sixteenths
    s.sections[0] = model::Section{0, 2, 1200, 4, 4};
    s.sections[1] = model::Section{1, 1, 900, 3, 4};
    s.order = {0, 1};
    s.play_mode = model::PlayMode::Song;
    s.tracks[0].name = "LEAD";
    s.tracks[3].name = "DRUMS";
    model::insert_note(s.patterns[0].lanes[0], model::Note{0, 480, 60, 100});
    model::insert_note(s.patterns[0].lanes[0], model::Note{1920, 960, 67, 90});
    model::insert_note(s.patterns[0].lanes[3], model::Note{0, 240, 36, 127});
    model::insert_note(s.patterns[1].lanes[0], model::Note{960, 1920, 64, 80});
    return d;
}

} // namespace

TEST_CASE("SMF export: type 1, 960 PPQ, tempo and signature per section, notes as played", "[m8][smf][ac]") {
    const model::Document doc = smf_doc();
    const auto bytes = io::export_smf(doc, "TEST SONG");
    REQUIRE(bytes.size() > 14);
    CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "MThd");
    CHECK(bytes[9] == 1);  // format 1
    CHECK(bytes[11] == 3); // conductor + LEAD + DRUMS
    CHECK((bytes[12] << 8 | bytes[13]) == 960);

    // Our own reader sees it as it was written.
    std::string error;
    const auto back = io::import_smf(bytes, model::Song{}, &error, 1);
    INFO(error);
    REQUIRE(back);
    CHECK(back->tracks == 2);
    CHECK(back->notes == 2 * 3 + 1); // section A twice
    CHECK(back->sections == 3);      // bars: A, A, B
    CHECK(back->song.sections[0].bpm10 == 1200);
    CHECK(back->song.sections[2].bpm10 == 900);
    CHECK(back->song.sections[2].sig_num == 3);
    CHECK(back->song.tracks[0].name == "LEAD");
    CHECK(back->song.tracks[1].name == "DRUMS");
    CHECK(back->song.patterns[1].lanes[0] == doc.song.patterns[0].lanes[0]); // second pass of A
    CHECK(back->song.patterns[2].lanes[0].front().tick == 960);

#if defined(PHOS_HAVE_LIBREMIDI)
    // An independent parser (libremidi's SMF reader) agrees on tempo, signature and notes.
    libremidi::reader r(true); // absolute ticks
    REQUIRE(r.parse(bytes) == libremidi::reader::validated);
    CHECK(r.format == 1);
    CHECK(r.ticksPerBeat == 960.0f);
    REQUIRE(r.tracks.size() == 3);
    std::vector<std::pair<int, int>> tempos; // tick, microseconds per quarter
    std::vector<std::pair<int, int>> sigs;   // tick, numerator
    for (const auto& e : r.tracks[0]) {
        if (e.m.get_meta_event_type() == libremidi::meta_event_type::TEMPO_CHANGE) {
            tempos.emplace_back(e.tick, e.m.bytes[3] << 16 | e.m.bytes[4] << 8 | e.m.bytes[5]);
        }
        if (e.m.get_meta_event_type() == libremidi::meta_event_type::TIME_SIGNATURE) {
            sigs.emplace_back(e.tick, e.m.bytes[3]);
        }
    }
    CHECK(tempos == std::vector<std::pair<int, int>>{{0, 500000}, {7680, 666667}});
    CHECK(sigs == std::vector<std::pair<int, int>>{{0, 4}, {7680, 3}});
    int note_ons = 0;
    int first_b = -1;
    for (const auto& e : r.tracks[1]) {
        if (e.m.get_message_type() == libremidi::message_type::NOTE_ON) {
            ++note_ons;
            if (e.tick >= 7680 && first_b < 0) {
                first_b = e.tick;
            }
        }
    }
    CHECK(note_ons == 5);
    CHECK(first_b == 7680 + 960);
#endif

    // Program change and CC automation travel too, on the track's MIDI channel.
    model::Document d2 = doc;
    d2.song.tracks[0].program = 4;
    d2.song.tracks[0].out_channel = 5;
    d2.song.patterns[0].lane_for(0, model::kCcVolume).values[4] = 90;
    const auto b2 = io::export_smf(d2, "T");
#if defined(PHOS_HAVE_LIBREMIDI)
    libremidi::reader r2(true);
    REQUIRE(r2.parse(b2) == libremidi::reader::validated);
    int programs = 0;
    int ccs = 0;
    for (const auto& e : r2.tracks[1]) {
        programs += e.m.bytes.size() == 2 && e.m.bytes[0] == 0xC5 && e.m.bytes[1] == 4 && e.tick == 0;
        ccs += e.m.bytes.size() == 3 && e.m.bytes[0] == 0xB5 && e.m.bytes[1] == 7 && e.m.bytes[2] == 90 && e.tick % 3840 == 960;
    }
    CHECK(programs == 1);
    CHECK(ccs == 2); // section A plays twice
#endif

    const auto dir = std::filesystem::temp_directory_path() / "phosphor_smf_test";
    std::filesystem::remove_all(dir);
    REQUIRE(io::write_smf(dir / "song.mid", doc, "TEST SONG", &error));
    CHECK(io::read_smf(dir / "song.mid", model::Song{}, &error));
    std::filesystem::remove_all(dir);
}

TEST_CASE("SMF import: type 0 split by channel, other PPQ, running status, truncation", "[m8][smf]") {
    // Hand-made type 0 file at 96 PPQ: tempo 100 BPM, 6/8; channel 1 note 60 at 0 (1 beat),
    // channel 10 note 36 at beat 1 with running status and a note-on velocity 0 as note-off.
    const std::vector<std::uint8_t> f = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,                        //
        'M', 'T', 'r', 'k', 0, 0, 0, 33,                                          //
        0, 0xFF, 0x51, 3, 0x09, 0x27, 0xC0,                                       // 600000 us = 100 BPM
        0, 0xFF, 0x58, 4, 6, 3, 24, 8,                                            // 6/8
        0, 0x90, 60, 100,                                                         //
        96, 0x80, 60, 0,                                                          //
        0, 0x99, 36, 120,                                                         //
        48, 36, 0,                                                                // running status, vel 0 = off
        0, 0xFF, 0x2F, 0,                                                         //
    };
    std::string error;
    const auto imp = io::import_smf(f, model::Song{}, &error);
    INFO(error);
    REQUIRE(imp);
    CHECK(imp->tracks == 2);
    CHECK(imp->notes == 2);
    CHECK(imp->song.sections[0].bpm10 == 1000);
    CHECK(imp->song.sections[0].sig_num == 6);
    CHECK(imp->song.sections[0].sig_den == 8);
    const auto& lead = imp->song.patterns[0].lanes[0];
    REQUIRE(lead.size() == 1);
    CHECK(lead[0].length == 960); // 96 ticks at 96 PPQ = one quarter
    const auto& drum = imp->song.patterns[0].lanes[1];
    REQUIRE(drum.size() == 1);
    CHECK(drum[0].tick == 960);
    CHECK(drum[0].length == 480);
    CHECK(imp->song.tracks[1].name == "CH 10");

    CHECK_FALSE(io::import_smf({'R', 'I', 'F', 'F'}, model::Song{}, &error));
    std::vector<std::uint8_t> cut(f.begin(), f.end() - 8);
    CHECK_FALSE(io::import_smf(cut, model::Song{}, &error));
}

// ---------------------------------------------------------------------------------------------
// MIDI out and automation in the engine
// ---------------------------------------------------------------------------------------------


namespace {

struct OutRig {
    engine::Engine eng;
    engine::SnapshotBuilder builder;
    model::Document doc;
    std::vector<engine::MidiOutEvent> sent;
    std::vector<float> audio; // mono mix

    OutRig() { eng.prepare(kRate, 2); }
    void publish(std::uint8_t clock_slots = 1, bool thru = true) {
        auto s = builder.build(doc, kRate);
        s->midi_out = true;
        s->open_slots = 0x03;
        s->clock_slots = clock_slots;
        s->thru = thru;
        for (int t = 0; t < model::kTracks; ++t) {
            s->track_out_slot[static_cast<std::size_t>(t)] = doc.song.tracks[static_cast<std::size_t>(t)].out_port == "B" ? 1 : 0;
        }
        eng.publish(std::move(s));
        render(64);
    }
    void render(std::size_t frames) {
        std::vector<float> buf(512);
        for (std::size_t pos = 0; pos < frames; pos += 256) {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(256, frames - pos));
            eng.render_offline(buf.data(), n, 2);
            for (std::uint32_t i = 0; i < n; ++i) {
                audio.push_back(0.5f * (buf[2 * i] + buf[2 * i + 1]));
            }
            engine::MidiOutEvent e;
            while (eng.pop_midi_out(e)) {
                sent.push_back(e);
            }
            eng.collect_garbage();
        }
    }
    std::vector<engine::MidiOutEvent> with(std::uint8_t status) const {
        std::vector<engine::MidiOutEvent> v;
        for (const auto& e : sent) {
            if (e.bytes[0] == status) {
                v.push_back(e);
            }
        }
        return v;
    }
};

} // namespace

TEST_CASE("MIDI out: external notes, program, start, 24 PPQN clock, stop, continue with SPP", "[m8][midiout]") {
    OutRig rig;
    model::Song& s = rig.doc.song;
    s.metronome = model::MetronomeMode::Off;
    s.sections[0].bpm10 = 1200; // beat = 24000 frames, clock = 1000 frames
    s.tracks[0].dest = model::MidiDest::External;
    s.tracks[0].out_channel = 2;
    s.tracks[0].program = 5;
    model::insert_note(s.patterns[0].lanes[0], model::Note{480, 240, 64, 99});
    rig.publish();
    const std::size_t start = rig.audio.size() + 64; // frame after this block boundary
    rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::Play));
    rig.render(kRate * 3 / 2); // 1.5 s: three beats
    REQUIRE(rig.with(0xFA).size() == 1);
    const std::uint64_t f0 = rig.with(0xFA)[0].frame;
    CHECK(f0 + 64 >= start);
    const auto prog = rig.with(0xC2);
    REQUIRE_FALSE(prog.empty());
    CHECK(prog[0].bytes[1] == 5);
    CHECK(prog[0].frame == f0);
    const auto on = rig.with(0x92);
    REQUIRE(on.size() == 1);
    CHECK(on[0].frame == f0 + 12000); // tick 480 at 120 BPM
    CHECK(on[0].bytes[1] == 64);
    CHECK(on[0].bytes[2] == 99);
    CHECK(rig.with(0x82).front().frame == f0 + 18000);
    const auto clock = rig.with(0xF8);
    REQUIRE(clock.size() >= 70);
    for (std::size_t i = 1; i < clock.size(); ++i) {
        REQUIRE(clock[i].frame - clock[i - 1].frame == 1000);
    }
    CHECK(clock[0].frame == f0);
    CHECK(rig.eng.read_meters().voices == 0); // EXTERNAL: no internal voice

    rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::Stop));
    rig.render(512);
    CHECK(rig.with(0xFC).size() == 1);
    const auto off = rig.with(0xB2);
    REQUIRE_FALSE(off.empty());
    CHECK(off.back().bytes[1] == 123); // all notes off on the track's channel

    rig.sent.clear();
    rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::Continue));
    rig.render(512);
    const auto spp = rig.with(0xF2);
    REQUIRE(spp.size() == 1);
    const int sixteenths = spp[0].bytes[1] | spp[0].bytes[2] << 7;
    CHECK(sixteenths == 12); // stopped after 3 beats (1.5 s)
    REQUIRE(rig.with(0xFB).size() == 1);
}

TEST_CASE("MIDI clock stays on a 40-tick grid across odd pattern lengths; count-in sends no clock", "[m8][midiout]") {
    OutRig rig;
    model::Song& s = rig.doc.song;
    s.metronome = model::MetronomeMode::Off;
    s.patterns[0].grid = model::Grid::SixtyFourth; // 60 ticks a step
    s.patterns[0].steps = 3;                        // 180 ticks: not a multiple of 40
    s.count_in_bars = 1;
    rig.publish();
    rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::PlayRecord));
    rig.render(kRate * 4); // 2 s count-in, then 2 s of play
    const auto start = rig.with(0xFA);
    REQUIRE(start.size() == 1);
    const auto clock = rig.with(0xF8);
    REQUIRE(clock.size() > 40);
    CHECK(clock[0].frame == start[0].frame);
    for (std::size_t i = 1; i < clock.size(); ++i) {
        REQUIRE(clock[i].frame - clock[i - 1].frame == 1000);
    }
}

TEST_CASE("THRU forwards live input to an external track; PANIC silences every channel", "[m8][midiout]") {
    OutRig rig;
    model::Song& s = rig.doc.song;
    s.tracks[0].dest = model::MidiDest::External;
    s.tracks[0].out_channel = 9;
    s.tracks[0].out_port = "B";
    rig.publish();
    rig.eng.send(engine::EngineCommand::midi_message(0x90, 60, 100, 0));
    rig.eng.send(engine::EngineCommand::midi_message(0xB0, 74, 33, 0));
    rig.render(512);
    const auto on = rig.with(0x99);
    REQUIRE(on.size() == 1);
    CHECK(on[0].slot == 1);
    CHECK(rig.with(0xB9).size() == 1);
    CHECK(rig.eng.read_meters().voices == 0);
    CHECK(rig.eng.read_midi_meters().held_note(60));

    rig.sent.clear();
    rig.eng.send(engine::EngineCommand::panic());
    rig.render(256);
    // Both open outputs, 16 channels, all sound off + all notes off.
    CHECK(rig.sent.size() == 2 * 16 * 2);

    s.tracks[0].dest = model::MidiDest::Both;
    rig.publish(1, false); // THRU off: nothing forwarded, the instrument plays
    rig.sent.clear();
    rig.eng.send(engine::EngineCommand::midi_message(0x90, 62, 100, 0));
    rig.render(512);
    CHECK(rig.sent.empty());
    CHECK(rig.eng.read_meters().voices == 1);
}

TEST_CASE("CC automation: volume and pan lanes shape the track inside PHOSPHOR", "[m8][automation]") {
    const auto level = [](int volume) {
        OutRig rig;
        model::Song& s = rig.doc.song;
        s.metronome = model::MetronomeMode::Off;
        rig.doc.instruments[0] = model::make_instrument(model::InstrumentKind::Synth, "PAD");
        rig.doc.instruments[0].sampler.amp = model::Adsr{1.0f, 10.0f, 1.0f, 50.0f};
        model::insert_note(s.patterns[0].lanes[0], model::Note{0, 3840, 57, 127});
        auto& lane = s.patterns[0].lane_for(0, model::kCcVolume);
        lane.values[0] = static_cast<std::int16_t>(volume);
        rig.publish();
        const std::size_t from = rig.audio.size();
        rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::Play));
        rig.render(kRate);
        double e = 0.0;
        for (std::size_t i = from + kRate / 2; i < rig.audio.size(); ++i) {
            e += static_cast<double>(rig.audio[i]) * rig.audio[i];
        }
        return std::sqrt(e / static_cast<double>(rig.audio.size() - from - kRate / 2));
    };
    const double full = level(127);
    const double half = level(64);
    const double none = level(0);
    CHECK(full > 0.01);
    CHECK(20.0 * std::log10(half / full) == Catch::Approx(40.0 * std::log10(64.0 / 127.0)).margin(0.5));
    CHECK(none < full * 1e-3);

    // The lane survives a project round trip and is compiled at its step (swing applied).
    model::Song s;
    s.patterns[0].lane_for(3, model::kCcPan).values[4] = 0;
    s.tracks[3].swing = true;
    s.swing = 66;
    const seq::CompiledSong c = seq::compile(s);
    REQUIRE(c.ccs.size() == 1);
    CHECK(c.ccs[0].tick == 4 * 240); // even step: not swung
    CHECK(c.ccs[0].cc == model::kCcPan);
}
