#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "model/workspace.h"
#include "seq/compiled_song.h"
#include "seq/sequencer.h"
#include "seq/timing.h"

using namespace phos;
using namespace phos::seq;
using Catch::Approx;

namespace {

model::Song one_bar_song(int bpm10, std::vector<std::uint32_t> ticks, int sig_num = 4, int sig_den = 4) {
    model::Song s;
    s.sections[0].bpm10 = bpm10;
    s.sections[0].sig_num = sig_num;
    s.sections[0].sig_den = sig_den;
    s.patterns[0].grid = model::Grid::Sixteenth;
    s.patterns[0].steps = bar_ticks(sig_num, sig_den) / 240;
    s.metronome = model::MetronomeMode::Off;
    s.count_in_bars = 0;
    for (const auto t : ticks) {
        model::insert_note(s.patterns[0].lanes[0], model::Note{t, 120, 60, 100});
    }
    return s;
}

std::vector<std::uint64_t> note_on_frames(Sequencer& seq, const CompiledSong& c, std::uint64_t from, std::uint64_t frames,
                                          std::uint32_t rate, std::uint32_t block) {
    std::vector<std::uint64_t> out;
    for (std::uint64_t f = from; f < from + frames; f += block) {
        seq.advance(c, f, block, rate, [&](const SeqEvent& e) {
            if (e.type == SeqEventType::NoteOn) {
                out.push_back(e.frame);
            }
        });
    }
    return out;
}

} // namespace

TEST_CASE("tick to frame conversion gives exact values", "[seq]") {
    CHECK(frame_of_tick(1, 48000, 1200) == 25);         // 120 BPM: 25 frames per tick
    CHECK(frame_of_tick(960, 48000, 1200) == 24000);    // one beat = 0.5 s
    CHECK(frame_of_tick(960, 44100, 1250) == 21168);    // 125 BPM at 44.1 kHz
    CHECK(frame_of_tick(960, 48000, 845) == 34082);     // 84.5 BPM: 34082.84 -> floor
    CHECK(frame_of_tick(bar_ticks(7, 8), 48000, 1400) == 72000); // a 7/8 bar at 140 BPM = 1.5 s
    CHECK(frame_of_tick(bar_ticks(6, 8), 96000, 200) == 864000);  // 20 BPM, 6/8: 9 s
    CHECK(frame_of_tick(bar_ticks(4, 4), 96000, 3000) == 76800);  // 300 BPM: 0.8 s per bar
    CHECK(bar_ticks(5, 16) == 1200);
    // tick_at_frame is the exact inverse: the tick sounding at a frame.
    for (const int bpm10 : {200, 845, 1200, 1337, 3000}) {
        for (const std::uint32_t rate : {44100u, 48000u, 96000u}) {
            for (std::uint64_t f = 0; f < 200000; f += 997) {
                const auto t = tick_at_frame(f, rate, bpm10);
                CHECK(frame_of_tick(t, rate, bpm10) <= f);
                CHECK(frame_of_tick(t + 1, rate, bpm10) > f);
            }
        }
    }
}

TEST_CASE("swing and bar:beat:tick formatting", "[seq]") {
    CHECK(swing_tick(0, 240, 66) == 0);
    CHECK(swing_tick(240, 240, 66) == 316); // the off-beat 16th moves to 66 % of the pair
    CHECK(swing_tick(480, 240, 66) == 480); // the next pair starts on time
    CHECK(swing_tick(240, 240, 50) == 240);
    CHECK(bar_beat_tick(0, 4, 4) == "001:1:000");
    CHECK(bar_beat_tick(3840 + 960 + 5, 4, 4) == "002:2:005");
    CHECK(bar_beat_tick(2880 + 480, 6, 8) == "002:2:000");
}

TEST_CASE("30 minutes of playback do not drift against the sample clock", "[seq]") {
    // 84.5 BPM at 44.1 kHz: neither a tick nor a bar is a whole number of frames.
    const auto song = one_bar_song(845, {0, 480, 1200, 3839});
    const CompiledSong c = compile(song);
    Sequencer seq;
    constexpr std::uint32_t kRate = 44100;
    const std::uint64_t start = 12345;
    seq.play(c, start, false);
    const std::uint64_t total = 30ull * 60 * kRate;
    // Odd block size, so pass ends fall everywhere inside blocks.
    const auto frames = note_on_frames(seq, c, start, total, kRate, 333);
    const std::uint64_t passes = frames.size() / 4;
    REQUIRE(passes > 600);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const std::uint64_t pass = i / 4;
        const std::uint32_t in_bar[4] = {0, 480, 1200, 3839};
        const std::uint64_t abs_tick = pass * 3840 + in_bar[i % 4];
        if (frames[i] != start + frame_of_tick(abs_tick, kRate, 845)) {
            FAIL("note " << i << " at frame " << frames[i] << ", expected " << start + frame_of_tick(abs_tick, kRate, 845));
        }
    }
    // The last note is within one frame of the exact musical time after 30 minutes.
    const std::size_t last = frames.size() - 1;
    const std::uint32_t last_in_bar[4] = {0, 480, 1200, 3839};
    const double abs_tick = static_cast<double>(last / 4 * 3840 + last_in_bar[last % 4]);
    const double exact = static_cast<double>(start) + abs_tick * kRate * 60.0 / (84.5 * 960.0);
    CHECK(static_cast<double>(frames.back()) <= exact);
    CHECK(exact - static_cast<double>(frames.back()) < 1.0);
    CHECK(static_cast<double>(frames.back() - start) / kRate > 29.0 * 60.0);
}

TEST_CASE("song mode plays sections with repeats and tempo changes exactly", "[seq]") {
    auto song = one_bar_song(1200, {0});
    song.play_mode = model::PlayMode::Song;
    song.sections[0].repeats = 2;          // A: 2 x 1 bar at 120
    song.sections[1] = song.sections[0];
    song.sections[1].repeats = 1;          // B: 1 bar at 90
    song.sections[1].bpm10 = 900;
    song.order = {0, 1};
    const CompiledSong c = compile(song);
    Sequencer seq;
    seq.play(c, 0, false);
    const auto frames = note_on_frames(seq, c, 0, 48000 * 12, 48000, 512);
    REQUIRE(frames.size() >= 4);
    CHECK(frames[0] == 0);
    CHECK(frames[1] == 96000);          // second A pass (2 s per bar at 120)
    CHECK(frames[2] == 192000);         // B starts after two bars
    CHECK(frames[3] == 192000 + 128000); // B lasts 2.666 s at 90 BPM, then the song loops
}

TEST_CASE("count-in delays the first bar, stop and continue keep the position", "[seq]") {
    auto song = one_bar_song(1200, {0, 1920});
    song.count_in_bars = 1;
    const CompiledSong c = compile(song);
    Sequencer seq;
    seq.play(c, 1000, true);
    CHECK(seq.state() == Sequencer::State::CountIn);
    int clicks = 0;
    std::vector<std::uint64_t> notes;
    for (std::uint64_t f = 1000; f < 1000 + 96000 + 48000; f += 256) {
        seq.advance(c, f, 256, 48000, [&](const SeqEvent& e) {
            clicks += (e.type == SeqEventType::Click || e.type == SeqEventType::AccentClick) ? 1 : 0;
            if (e.type == SeqEventType::NoteOn) {
                notes.push_back(e.frame);
            }
        });
    }
    REQUIRE(!notes.empty());
    CHECK(notes[0] == 1000 + 96000); // after one 4/4 bar of clicks at 120 BPM
    CHECK(clicks >= 4);
    CHECK(seq.recording());

    seq.stop(c, 1000 + 96000 + 24000, 48000); // stopped one beat into the bar
    CHECK(seq.tick_at(c, 0, 48000) == 960);
    seq.resume(c, 500000, 48000);
    notes.clear();
    for (std::uint64_t f = 500000; f < 500000 + 96000; f += 256) {
        seq.advance(c, f, 256, 48000, [&](const SeqEvent& e) {
            if (e.type == SeqEventType::NoteOn) {
                notes.push_back(e.frame);
            }
        });
    }
    REQUIRE(!notes.empty());
    CHECK(notes[0] == 500000 + 24000); // tick 1920 was one beat ahead of the stop point
}

TEST_CASE("recorded notes land within 1 ms of when they were played (STABLE)", "[seq][engine]") {
    constexpr std::uint32_t kRate = 48000;
    constexpr std::uint32_t kBlock = 64;
    constexpr double kPeriodNs = 1e9 * kBlock / kRate;
    constexpr std::int64_t kT0 = 7'000'000'000;
    auto e = std::make_unique<engine::Engine>();
    e->prepare(kRate, 2);
    auto snap = std::make_unique<engine::EngineSnapshot>();
    snap->placement = engine::Placement::Stable;
    snap->song = compile(one_bar_song(1200, {}));
    e->publish(std::move(snap));
    std::vector<float> out(kBlock * 2);
    long block = 0;
    auto now = [&] { return kT0 + static_cast<std::int64_t>(static_cast<double>(block) * kPeriodNs); };
    auto run = [&](int n) {
        for (int i = 0; i < n; ++i) {
            e->process(out.data(), kBlock, 2, now());
            ++block;
        }
    };
    run(40); // lock the DLL
    e->send(engine::EngineCommand::transport(engine::TransportAction::PlayRecord));
    const std::int64_t t_play = now(); // the block that starts playback begins here
    run(1);
    REQUIRE(e->read_transport().recording);

    // Keys pressed at musical positions (ticks) with +-0.3 ms human offsets; MIDI arrives later.
    const std::uint32_t ticks[] = {0, 240, 960, 1500, 2880, 3700};
    const double offsets_ms[] = {0.0, 0.3, -0.2, 0.25, -0.3, 0.1};
    const double ns_per_tick = 60e9 / (120.0 * 960.0);
    for (int i = 0; i < 6; ++i) {
        const auto ts = t_play + static_cast<std::int64_t>((ticks[i] + 3840) * ns_per_tick + offsets_ms[i] * 1e6);
        while (now() < ts) {
            run(1);
        }
        const std::uint8_t bytes[3] = {0x90, static_cast<std::uint8_t>(60 + i), 100};
        e->midi_queues().push_bytes(0, bytes, 3, ts); // second pass, so "before the pass" cannot occur
        run(1);
    }
    std::vector<engine::RecEvent> rec;
    engine::RecEvent r;
    while (e->pop_rec(r)) {
        rec.push_back(r);
    }
    REQUIRE(rec.size() == 6);
    const double ticks_per_ms = 120.0 * 960.0 / 60000.0; // 1.92
    for (int i = 0; i < 6; ++i) {
        const double expected = ticks[i] + offsets_ms[i] * ticks_per_ms;
        INFO("note " << i << " recorded at tick " << rec[static_cast<std::size_t>(i)].tick << ", played at " << expected);
        CHECK(std::fabs(rec[static_cast<std::size_t>(i)].tick - expected) <= ticks_per_ms); // within 1 ms
        CHECK(rec[static_cast<std::size_t>(i)].on);
    }
}

TEST_CASE("engine plays sequenced notes sample-accurately on their track", "[seq][engine]") {
    auto song = one_bar_song(1200, {10});
    song.tracks[0].instrument = 3; // empty instrument: test sine
    engine::Engine e;
    auto snap = std::make_unique<engine::EngineSnapshot>();
    snap->song = compile(song);
    e.publish(std::move(snap));
    std::vector<float> out(512 * 2);
    e.render_offline(out.data(), 512, 2); // swap the snapshot in
    e.send(engine::EngineCommand::transport(engine::TransportAction::Play));
    e.render_offline(out.data(), 512, 2);
    int first = -1;
    for (int i = 0; i < 512; ++i) {
        if (out[static_cast<std::size_t>(i) * 2] != 0.0f) {
            first = i;
            break;
        }
    }
    CHECK(first >= 250); // tick 10 = 250 frames at 120 BPM / 48 kHz
    CHECK(first <= 252);
    const auto peaks = e.read_track_peaks();
    CHECK(peaks[0] > 0.0f);
    CHECK(peaks[1] == 0.0f);
    e.send(engine::EngineCommand::transport(engine::TransportAction::Stop));
    std::vector<float> tail(48000 * 2);
    e.render_offline(tail.data(), 48000, 2);
    CHECK(e.read_meters().voices == 0);
}

TEST_CASE("muted and soloed tracks", "[seq]") {
    model::Song s;
    s.tracks[2].mute = true;
    s.tracks[5].solo = true;
    const CompiledSong c = compile(s);
    CHECK(c.tracks[2].gain == 0.0f);
    CHECK(c.tracks[5].gain == Approx(1.0));
    CHECK(c.tracks[0].gain == 0.0f); // not soloed while another track is
}

TEST_CASE("a recording take is one undo step", "[model]") {
    model::Workspace ws;
    ws.begin_take();
    for (int i = 0; i < 5; ++i) {
        model::insert_note(ws.take_song().patterns[0].lanes[0], model::Note{static_cast<std::uint32_t>(i * 240), 120, 60, 100});
    }
    ws.commit_take("RECORD");
    CHECK(ws.undo_depth() == 1);
    CHECK(ws.doc().song.patterns[0].lanes[0].size() == 5);
    CHECK(ws.undo() == "RECORD");
    CHECK(ws.doc().song.patterns[0].lanes[0].empty());

    auto song = ws.doc().song;
    song.sections[0].bpm10 = 900;
    ws.set_song(song, "TEMPO");
    CHECK(ws.undo() == "TEMPO");
    CHECK(ws.doc().song.sections[0].bpm10 == 1200);
}

#include <filesystem>
#include <fstream>
#include <sstream>

#include "io/import.h"
#include "io/project.h"

TEST_CASE("save -> load -> save produces identical project JSON", "[project]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "phosphor_project_test";
    fs::remove_all(dir);
    model::Document doc;
    // An imported, edited sample with a loop, plus a song with notes, sections and track settings.
    auto r = io::import_sample(fs::path(PHOS_FIXTURE_DIR) / "loop_pingpong.wav", {});
    REQUIRE(r.ok());
    doc.instruments[2].name = "PING";
    doc.instruments[2].sample = model::edit::normalize(*r.sample);
    doc.instruments[2].sampler = r.params;
    doc.instruments[2].sampler.cutoff_hz = 1234.5f;
    doc.instruments[2].sampler.filter = model::FilterType::BandPass;
    doc.current = 2;
    model::Song& s = doc.song;
    s.patterns.push_back(model::Pattern{"VERSE"});
    s.patterns[1].steps = 32;
    s.patterns[1].grid = model::Grid::EighthT;
    model::insert_note(s.patterns[0].lanes[0], model::Note{0, 240, 60, 100});
    model::insert_note(s.patterns[0].lanes[5], model::Note{720, 100, 67, 77});
    model::insert_note(s.patterns[1].lanes[15], model::Note{1600, 3000, 30, 127});
    s.sections[1] = model::Section{1, 3, 845, 7, 8};
    s.order = {0, 1, 1, 0};
    s.tracks[5].mute = true;
    s.tracks[5].pan = -0.25f;
    s.tracks[2].volume_db = -3.5f;
    s.swing = 62;
    s.play_mode = model::PlayMode::Song;

    std::string error;
    REQUIRE(io::save_project(dir / "Test.phosphor", doc, &error));
    auto read = [&] {
        std::ifstream in(dir / "Test.phosphor" / "project.json");
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const std::string first = read();
    const auto loaded = io::load_project(dir / "Test.phosphor", 48000, &error);
    REQUIRE(loaded);
    CHECK(loaded->song == doc.song);
    CHECK(loaded->instruments[2].sampler == doc.instruments[2].sampler);
    CHECK(loaded->instruments[2].sample->left == doc.instruments[2].sample->left);
    REQUIRE(io::save_project(dir / "Test.phosphor", *loaded, &error));
    CHECK(read() == first);
    CHECK(io::project_json(*loaded) == first);
    fs::remove_all(dir);
}

TEST_CASE("project bundle names and schema versions", "[project]") {
    CHECK(io::bundle_path("/p", "My Song") == std::filesystem::path("/p/My Song.phosphor"));
    CHECK(io::bundle_path("/p", "/x/a.phosphor") == std::filesystem::path("/x/a.phosphor"));
    std::string text = R"({"schemaVersion": 99})";
    std::string error;
    CHECK_FALSE(io::migrate_project(text, &error));
    CHECK(error.find("99") != std::string::npos);
}
