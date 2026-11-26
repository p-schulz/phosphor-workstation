#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <vector>

#include "core/notes.h"
#include "core/rt_guard.h"
#include "dsp/yin.h"
#include "engine/engine.h"
#include "io/import.h"
#include "model/workspace.h"

using namespace phos;
using Catch::Approx;

namespace {

const std::filesystem::path kFixtures = PHOS_FIXTURE_DIR;

model::SampleRef sine_sample(double hz, double seconds, std::uint32_t rate = 48000, bool stereo = false) {
    const auto n = static_cast<std::size_t>(seconds * rate);
    std::vector<float> l(n);
    for (std::size_t i = 0; i < n; ++i) {
        l[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    }
    std::vector<float> r;
    if (stereo) {
        r = l;
    }
    return model::make_sample("sine", "", rate, std::move(l), std::move(r));
}

double rising_crossings_hz(const std::vector<float>& interleaved, std::size_t skip, std::uint32_t rate = 48000) {
    int crossings = 0;
    const std::size_t frames = interleaved.size() / 2;
    for (std::size_t i = skip + 1; i < frames; ++i) {
        crossings += (interleaved[(i - 1) * 2] < 0.0f && interleaved[i * 2] >= 0.0f) ? 1 : 0;
    }
    return crossings * static_cast<double>(rate) / static_cast<double>(frames - skip);
}

// Engine with instrument 0 = `sample` and parameters `p`.
std::unique_ptr<engine::Engine> engine_with(const model::SampleRef& sample, const model::SamplerParams& p) {
    auto e = std::make_unique<engine::Engine>();
    auto s = std::make_unique<engine::EngineSnapshot>();
    s->instruments[0] = engine::sampler_block(sample.get(), p);
    s->sample_refs[0] = sample;
    e->publish(std::move(s));
    return e;
}

std::vector<float> render(engine::Engine& e, std::size_t frames) {
    std::vector<float> out(frames * 2);
    for (std::size_t pos = 0; pos < frames; pos += 256) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(256, frames - pos));
        e.render_offline(out.data() + pos * 2, n, 2);
    }
    return out;
}

float peak(const std::vector<float>& v, std::size_t from = 0, std::size_t to = SIZE_MAX) {
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, v.size()); ++i) {
        p = std::max(p, std::fabs(v[i]));
    }
    return p;
}

} // namespace

TEST_CASE("note names from file names", "[import]") {
    CHECK(note_from_filename("Piano_C3") == 48);
    CHECK(note_from_filename("bass F#2 soft") == 42);
    CHECK(note_from_filename("pad-60") == 60);
    CHECK(note_from_filename("kick_01") == std::nullopt);
    CHECK(note_from_filename("loop_120bpm") == std::nullopt);
    CHECK(note_from_filename("organ C-1") == 0);
    CHECK(note_name(61) == "C#4");
}

TEST_CASE("every fixture format imports at the engine rate", "[import]") {
    for (const char* name : {"pcm8_A3.wav", "pcm16_A3.wav", "pcm24_A3.wav", "pcm32_A3.wav", "float32_A3.wav",
                             "tone_A3.aiff", "tone_A3.flac", "tone_A3.mp3", "tone_A3.ogg"}) {
        INFO(name);
        const auto r = io::import_sample(kFixtures / name, {});
        REQUIRE(r.ok());
        const auto& s = *r.sample;
        CHECK(s.sample_rate == 48000);
        CHECK(s.seconds() > 0.2);
        CHECK(s.seconds() < 0.62); // MP3 adds encoder padding
        CHECK(r.params.root_note == 57); // "A3" in the name
        CHECK(r.root_source == "NAME");
        CHECK(s.peak() > 0.2f);
        CHECK(s.overview.max.size() == model::Overview::kBins);
        // Still 220 Hz after decoding and 44.1 -> 48 kHz resampling.
        const std::size_t from = s.frames / 4;
        const auto p = dsp::yin(std::span<const float>(s.left).subspan(from, 4096), s.sample_rate);
        CHECK(p.hz == Approx(220.0).margin(1.5));
    }
    CHECK(io::import_sample(kFixtures / "tone_A3.aiff", {}).sample->channels == 2);
    CHECK_FALSE(io::import_sample(kFixtures / "make_fixtures.sh", {}).ok());
}

TEST_CASE("smpl loops and unity note are honoured", "[import]") {
    auto r = io::import_sample(kFixtures / "loop_forward.wav", {});
    REQUIRE(r.ok());
    CHECK(r.params.loop == model::LoopMode::Forward);
    CHECK(r.params.loop_start == 12000);
    CHECK(r.params.loop_end == 36000);
    CHECK(r.params.root_note == 64);
    CHECK(r.root_source == "SMPL");
    r = io::import_sample(kFixtures / "loop_pingpong.wav", {});
    CHECK(r.params.loop == model::LoopMode::PingPong);
}

TEST_CASE("root key from pitch detection when the name has none", "[import]") {
    const auto r = io::import_sample(kFixtures / "pitched_tone.wav", {});
    REQUIRE(r.ok());
    CHECK(r.root_source == "PITCH");
    CHECK(r.params.root_note == 55); // G3
}

TEST_CASE("sampler plays at the note's pitch relative to the root", "[sampler]") {
    model::SamplerParams p;
    p.root_note = 57; // the sample is a 220 Hz A3
    p.loop = model::LoopMode::Forward;
    const auto s = sine_sample(220.0, 0.5);
    p.loop_end = s->frames;
    for (const auto interp : {model::Interp::Drop, model::Interp::Linear, model::Interp::Hermite}) {
        p.interp = interp;
        auto e = engine_with(s, p);
        e->send(engine::EngineCommand::midi_message(0x90, 69, 127, 0)); // A4: one octave up
        const auto out = render(*e, 48000);
        CHECK(rising_crossings_hz(out, 4800) == Approx(440.0).margin(2.0));
        CHECK(peak(out, 48000) > 0.3f); // loops for the whole second
    }
}

TEST_CASE("one-shot ends at the sample end, release ends a looped note", "[sampler]") {
    const auto s = sine_sample(220.0, 0.1);
    model::SamplerParams p;
    p.root_note = 60;
    auto e = engine_with(s, p);
    e->send(engine::EngineCommand::midi_message(0x90, 60, 127, 0));
    auto out = render(*e, 9600);
    CHECK(peak(out, 0, 4000 * 2) > 0.3f);
    CHECK(peak(out, 4900 * 2) == 0.0f); // 0.1 s = 4800 frames
    CHECK(e->read_meters().voices == 0);

    p.loop = model::LoopMode::PingPong;
    p.loop_start = 1000;
    p.loop_end = 4000;
    p.amp.release_ms = 50.0f;
    e = engine_with(s, p);
    e->send(engine::EngineCommand::midi_message(0x90, 60, 127, 0));
    out = render(*e, 48000);
    CHECK(peak(out, 40000 * 2) > 0.3f); // ping-pong keeps it going
    e->send(engine::EngineCommand::midi_message(0x80, 60, 0, 0));
    out = render(*e, 9600);
    CHECK(peak(out, 4800 * 2) == 0.0f);
    CHECK(e->read_meters().voices == 0);
}

TEST_CASE("low-pass filter removes highs, key tracking and velocity move the cutoff", "[sampler]") {
    const auto s = sine_sample(4000.0, 0.5);
    model::SamplerParams p;
    p.root_note = 60;
    p.filter = model::FilterType::LowPass;
    p.cutoff_hz = 500.0f;
    auto e = engine_with(s, p);
    e->send(engine::EngineCommand::midi_message(0x90, 60, 127, 0));
    const float filtered = peak(render(*e, 12000), 4800 * 2);
    p.filter = model::FilterType::Off;
    e = engine_with(s, p);
    e->send(engine::EngineCommand::midi_message(0x90, 60, 127, 0));
    const float dry = peak(render(*e, 12000), 4800 * 2);
    CHECK(filtered < dry * 0.1f); // 3 octaves above a 12 dB/oct cutoff: about -36 dB

    p.filter = model::FilterType::LowPass;
    p.vel_to_cutoff = 4.0f; // full velocity opens 4 octaves: 8 kHz
    e = engine_with(s, p);
    e->send(engine::EngineCommand::midi_message(0x90, 60, 127, 0));
    CHECK(peak(render(*e, 12000), 4800 * 2) > dry * 0.7f);
}

TEST_CASE("a replaced sample stays alive while a voice still plays it", "[sampler][engine]") {
    auto a = sine_sample(220.0, 1.0);
    model::SamplerParams p;
    p.loop = model::LoopMode::Forward;
    p.loop_end = a->frames;
    std::weak_ptr<const model::SampleData> watch = a;
    auto e = engine_with(a, p);
    e->send(engine::EngineCommand::midi_message(0x90, 60, 127, 0));
    render(*e, 1024);
    e->collect_garbage(); // initial snapshot
    a.reset();

    // New snapshot with a different sample: the playing voice keeps the old one.
    auto b = sine_sample(330.0, 1.0);
    auto s = std::make_unique<engine::EngineSnapshot>();
    s->instruments[0] = engine::sampler_block(b.get(), p);
    s->sample_refs[0] = b;
    e->publish(std::move(s));
    render(*e, 1024);
    e->collect_garbage();
    CHECK_FALSE(watch.expired()); // still referenced by the retired snapshot
    const auto playing = render(*e, 4800);
    CHECK(peak(playing) > 0.3f);

    e->send(engine::EngineCommand::midi_message(0x80, 60, 0, 0));
    render(*e, 48000); // release ends the voice
    e->collect_garbage();
    CHECK(watch.expired());
}

TEST_CASE("sample edits and undo", "[sampler][model]") {
    model::Workspace ws;
    model::Instrument inst;
    inst.name = "SINE";
    inst.sample = sine_sample(220.0, 1.0, 48000, true);
    ws.set_instrument(0, inst, "IMPORT");
    const auto original = ws.doc().instruments[0].sample;

    auto edited = ws.doc().instruments[0];
    edited.sample = model::edit::trim(*edited.sample, 12000, 36000);
    ws.set_instrument(0, edited, "TRIM");
    CHECK(ws.doc().instruments[0].sample->frames == 24000);

    edited.sample = model::edit::normalize(*edited.sample, -0.3f);
    ws.set_instrument(0, edited, "NORMALIZE");
    CHECK(ws.doc().instruments[0].sample->peak() == Approx(std::pow(10.0, -0.3 / 20.0)).epsilon(1e-4));

    const auto before_rev = ws.doc().instruments[0].sample;
    edited.sample = model::edit::reverse(*edited.sample, 0, edited.sample->frames);
    ws.set_instrument(0, edited, "REVERSE");
    CHECK(ws.doc().instruments[0].sample->left.front() == before_rev->left.back());

    edited.sampler.loop = model::LoopMode::Forward;
    edited.sampler.loop_start = 1000;
    edited.sampler.loop_end = 20000;
    ws.set_instrument(0, edited, "LOOP");

    CHECK(ws.undo() == "LOOP");
    CHECK(ws.doc().instruments[0].sampler.loop == model::LoopMode::Off);
    CHECK(ws.undo() == "REVERSE");
    CHECK(ws.doc().instruments[0].sample == before_rev);
    CHECK(ws.undo() == "NORMALIZE");
    CHECK(ws.undo() == "TRIM");
    CHECK(ws.doc().instruments[0].sample == original);
    CHECK(ws.redo() == "TRIM");
    CHECK(ws.doc().instruments[0].sample->frames == 24000);
}

TEST_CASE("repeated changes of one field merge into one undo step", "[model]") {
    model::Workspace ws;
    model::Instrument inst;
    for (int i = 1; i <= 10; ++i) {
        inst.sampler.cutoff_hz = 1000.0f + static_cast<float>(i);
        ws.set_instrument(0, inst, "CUTOFF", 5.0 + i * 0.1);
    }
    CHECK(ws.undo_depth() == 1);
    inst.sampler.cutoff_hz = 9000.0f;
    ws.set_instrument(0, inst, "CUTOFF", 20.0); // later: a new step
    CHECK(ws.undo_depth() == 2);
    ws.undo();
    CHECK(ws.doc().instruments[0].sampler.cutoff_hz == 1010.0f);
}

TEST_CASE("fades, crossfade loop", "[sampler][model]") {
    const auto s = sine_sample(100.0, 1.0);
    const auto in = model::edit::fade_in(*s, 0, 48000);
    CHECK(std::fabs(in->left[10]) < 0.001f);
    const auto out = model::edit::fade_out(*s, 24000, 48000);
    CHECK(std::fabs(out->left[47999]) < 0.001f);
    CHECK(out->left[100] == s->left[100]);
    // Crossfading a loop whose ends do not match removes the jump at the wrap point.
    const auto x = model::edit::crossfade_loop(*s, 10000, 30125, 2000);
    const float jump_before = std::fabs(s->left[30124] - s->left[10000]);
    const float jump_after = std::fabs(x->left[30124] - x->left[10000]);
    CHECK(jump_before > 0.05f);
    CHECK(jump_after < 0.01f);
}

TEST_CASE("32 stereo sampler voices use less than a quarter of one core", "[sampler][perf]") {
    const auto s = sine_sample(110.0, 2.0, 48000, true);
    model::SamplerParams p;
    p.interp = model::Interp::Hermite;
    p.filter = model::FilterType::LowPass;
    p.loop = model::LoopMode::Forward;
    p.loop_end = s->frames;
    auto e = engine_with(s, p);
    for (int n = 0; n < 32; ++n) {
        e->send(engine::EngineCommand::midi_message(0x90, static_cast<std::uint8_t>(36 + n), 100, 0));
    }
    std::vector<float> out(128 * 2);
    e->render_offline(out.data(), 128, 2);
    REQUIRE(e->read_meters().voices == 32);
    constexpr int kBlocks = 3750; // 10 s at 48 kHz in 128-frame blocks
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < kBlocks; ++b) {
        e->render_offline(out.data(), 128, 2);
    }
    const double cpu_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double load = cpu_s / 10.0;
    WARN("32 stereo Hermite + SVF voices: " << load * 100.0 << " % of one core");
#if defined(NDEBUG)
    CHECK(load < 0.25);
#endif
}

TEST_CASE("sampler playback and snapshot swaps stay allocation-free", "[sampler][realtime]") {
    if (!rt::guard_enabled()) {
        SKIP("allocation guard is compiled in Debug builds only");
    }
    rt::set_abort_on_violation(false);
    const auto a = sine_sample(220.0, 0.5, 48000, true);
    const auto b = sine_sample(330.0, 0.5);
    model::SamplerParams p;
    p.loop = model::LoopMode::PingPong;
    p.loop_start = 1000;
    p.loop_end = 20000;
    p.filter = model::FilterType::BandPass;
    auto e = engine_with(a, p);
    std::vector<float> out(512 * 2);
    const auto before = rt::violations();
    for (int block = 0; block < 600; ++block) {
        if (block % 5 == 0) {
            e->send(engine::EngineCommand::midi_message(0x90, static_cast<std::uint8_t>(40 + block % 30), 100, 0));
        }
        if (block % 7 == 0) {
            e->send(engine::EngineCommand::midi_message(0x80, static_cast<std::uint8_t>(40 + (block + 10) % 30), 0, 0));
        }
        if (block % 20 == 0) { // live edit: new snapshot, sometimes with a different sample
            auto s = std::make_unique<engine::EngineSnapshot>();
            const auto& smp = block % 40 == 0 ? b : a;
            p.cutoff_hz = 300.0f + static_cast<float>(block * 10);
            s->instruments[0] = engine::sampler_block(smp.get(), p);
            s->sample_refs[0] = smp;
            e->publish(std::move(s));
        }
        e->process(out.data(), 512, 2, 0);
        e->collect_garbage(); // UI thread side: frees released snapshots outside the scope
    }
    CHECK(rt::violations() == before);
    rt::set_abort_on_violation(true);
}
