// M7 vaporwave toolkit: time-stretch, loops, FX, VAPOR macro, templates (PLAN.md §7.5).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "content/templates.h"
#include "core/rt_guard.h"
#include "dsp/fx_units.h"
#include "dsp/stretch.h"
#include "engine/engine.h"
#include "engine/snapshot_builder.h"
#include "io/project.h"
#include "model/fx.h"
#include "model/song.h"
#include "model/wavetable.h"
#include "model/workspace.h"

using namespace phos;

namespace {

constexpr std::uint32_t kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double hz, std::size_t frames, float amp = 0.5f) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        v[i] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    }
    return v;
}

struct Noise {
    std::uint32_t state = 7;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
};

// Frequency from rising zero crossings in [from, to).
double zero_cross_hz(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double first = -1.0;
    double last = -1.0;
    int n = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
        if (v[i - 1] < 0.0f && v[i] >= 0.0f) {
            const double x = static_cast<double>(i - 1) + v[i - 1] / (v[i - 1] - v[i]);
            if (first < 0.0) {
                first = x;
            } else {
                ++n;
            }
            last = x;
        }
    }
    return n > 0 ? n * kRate / (last - first) : 0.0;
}

} // namespace

TEST_CASE("STRETCH keeps the pitch and the requested length; SHIFT transposes", "[vapor][stretch]") {
    const auto src = sine(440.0, kRate * 2);
    const dsp::StretchInput in{src.data(), nullptr, src.size(), kRate};
    const auto slow = dsp::stretch(in, kRate * 3, 0.0f, false);
    REQUIRE(slow.left.size() == kRate * 3);
    CHECK(slow.right.empty());
    CHECK(zero_cross_hz(slow.left, kRate / 2, kRate * 5 / 2) == Catch::Approx(440.0).epsilon(0.01));
    const auto up = dsp::stretch(in, kRate * 3, 12.0f, false);
    CHECK(zero_cross_hz(up.left, kRate / 2, kRate * 5 / 2) == Catch::Approx(880.0).epsilon(0.01));
}

TEST_CASE("STRETCH of a loop wraps without a gap", "[vapor][stretch]") {
    // Exactly 100 cycles of 400 Hz: a perfect loop. The stretched loop must stay continuous across
    // its end -> start seam (no step larger than the sine's own slope allows).
    const auto src = sine(400.0, kRate / 4);
    const dsp::StretchInput in{src.data(), nullptr, src.size(), kRate};
    const auto out = dsp::stretch(in, kRate / 3, 0.0f, true);
    const float seam = std::fabs(out.left.front() - out.left.back());
    CHECK(seam < 0.08f); // max slope of 0.5 sin(2 pi 400 t) at 48 kHz: 0.026 per sample (+ stretch smear)
}

TEST_CASE("STRETCH cache for an 8-bar loop is ready in under 2 s", "[vapor][stretch][ac]") {
    // PLAN.md M7 AC. 8 bars of 4/4 at 90 BPM (21.3 s, stereo) stretched to 120 BPM.
    const std::size_t frames = static_cast<std::size_t>(8 * 4 * 60.0 / 90.0 * kRate);
    std::vector<float> l(frames);
    std::vector<float> r(frames);
    std::uint32_t seed = 1;
    for (std::size_t i = 0; i < frames; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
        const float env = std::exp(-static_cast<float>(i % (kRate * 2 / 3)) / 4000.0f); // a hit every beat
        l[i] = 0.3f * env * noise + 0.2f * std::sin(static_cast<float>(i) * 0.05f);
        r[i] = 0.3f * env * noise + 0.2f * std::sin(static_cast<float>(i) * 0.051f);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = dsp::stretch(dsp::StretchInput{l.data(), r.data(), frames, kRate}, frames * 90 / 120, 0.0f, true);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("stretch 8 bars (%.1f s stereo) 90 -> 120 BPM: %.3f s\n", static_cast<double>(frames) / kRate, s);
    CHECK(out.left.size() == frames * 90 / 120);
    CHECK(out.right.size() == out.left.size());
    CHECK(s < 2.0);
}

namespace {

struct Stereo {
    std::vector<float> l;
    std::vector<float> r;
};

// Runs `unit` over `in` in 256-frame blocks (the engine's typical block).
Stereo run_fx(dsp::FxUnit& unit, const model::FxSlot& slot, Stereo in, double bpm = 120.0,
              const Stereo* side = nullptr) {
    for (std::size_t pos = 0; pos < in.l.size(); pos += 256) {
        const std::size_t n = std::min<std::size_t>(256, in.l.size() - pos);
        dsp::FxContext ctx{&slot, bpm, side ? side->l.data() + pos : nullptr, side ? side->r.data() + pos : nullptr};
        unit.process(in.l.data() + pos, in.r.data() + pos, n, ctx);
    }
    return in;
}

Stereo impulse(std::size_t frames) {
    Stereo s{std::vector<float>(frames), std::vector<float>(frames)};
    s.l[0] = s.r[0] = 1.0f;
    return s;
}

double rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        e += static_cast<double>(v[i]) * v[i];
    }
    return std::sqrt(e / static_cast<double>(std::max<std::size_t>(1, to - from)));
}

} // namespace

TEST_CASE("FX parameter table: defaults in range, names round-trip", "[vapor][fx]") {
    for (int t = 0; t < model::kFxTypes; ++t) {
        const auto type = static_cast<model::FxType>(t);
        model::FxType back{};
        REQUIRE(model::fx_from_name(model::fx_name(type), back));
        CHECK(back == type);
        const model::FxSlot s = model::make_fx(type);
        model::FxSlot c = s;
        model::clamp_fx(c);
        CHECK(c == s);
    }
    model::FxType none{};
    CHECK_FALSE(model::fx_from_name("FLANGER", none));
}

TEST_CASE("CRUSH quantises to BITS and holds samples at RATE", "[vapor][fx]") {
    dsp::CrushFx fx(kRate);
    model::FxSlot slot = model::make_fx(model::FxType::Crush);
    slot.p = {4.0f, 8000.0f, 0.0f, 0.0f, 1.0f};
    auto s = sine(1000.0, 4800);
    const Stereo out = run_fx(fx, slot, Stereo{s, s});
    for (std::size_t i = 0; i < out.l.size(); ++i) {
        const float q = out.l[i] * 8.0f; // 2^(4-1) steps per polarity
        REQUIRE(std::fabs(q - std::round(q)) < 1e-5f);
    }
    int changes = 0;
    for (std::size_t i = 1; i < out.l.size(); ++i) {
        changes += out.l[i] != out.l[i - 1];
    }
    CHECK(changes <= 4800 / 6 + 1); // at most one new value per 6 frames (48000 / 8000)
}

TEST_CASE("LOFI low-passes: 5 kHz down by more than 18 dB at CUTOFF 500 Hz, 100 Hz passes", "[vapor][fx]") {
    model::FxSlot slot = model::make_fx(model::FxType::Lofi);
    slot.p = {500.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (const double hz : {100.0, 5000.0}) {
        dsp::LofiFx fx(kRate);
        const auto s = sine(hz, kRate / 2);
        const Stereo out = run_fx(fx, slot, Stereo{s, s});
        const double gain_db = 20.0 * std::log10(rms(out.l, kRate / 4, kRate / 2) / rms(s, kRate / 4, kRate / 2));
        if (hz < 1000.0) {
            CHECK(gain_db > -1.0);
        } else {
            CHECK(gain_db < -18.0);
        }
    }
}

TEST_CASE("DELAY repeats on the beat grid, ping-pong alternates sides, feedback decays", "[vapor][fx]") {
    dsp::DelayFx fx(kRate);
    model::FxSlot slot = model::make_fx(model::FxType::Delay);
    slot.p = {2.0f /* 1/8 */, 0.5f, 20000.0f, 1.0f /* ping */, 1.0f /* wet only */};
    const Stereo out = run_fx(fx, slot, impulse(kRate), 120.0);
    const std::size_t eighth = kRate / 4; // 1/8 at 120 BPM = 0.25 s
    const auto peak_near = [](const std::vector<float>& v, std::size_t at) {
        float m = 0.0f;
        for (std::size_t i = at - 3; i <= at + 3; ++i) {
            m = std::max(m, std::fabs(v[i]));
        }
        return m;
    };
    CHECK(peak_near(out.l, eighth) > 0.3f);       // first repeat: left
    CHECK(peak_near(out.r, eighth) < 0.01f);
    CHECK(peak_near(out.r, 2 * eighth) > 0.1f);   // second: right, quieter
    CHECK(peak_near(out.r, 2 * eighth) < peak_near(out.l, eighth));
    CHECK(rms(out.l, 1, eighth - 8) < 1e-6);      // nothing before the first repeat
}

TEST_CASE("PLATE: a decaying, decorrelated stereo tail; longer DECAY rings longer", "[vapor][fx]") {
    const auto tail = [](float decay, Stereo* keep) {
        dsp::PlateFx fx(kRate);
        model::FxSlot slot = model::make_fx(model::FxType::Plate);
        slot.p = {decay, 0.3f, 0.0f, 1.0f, 1.0f};
        const Stereo out = run_fx(fx, slot, impulse(kRate * 3));
        if (keep) {
            *keep = out;
        }
        return std::pair{rms(out.l, kRate / 10, kRate / 2), rms(out.l, kRate * 2, kRate * 3)};
    };
    Stereo out;
    const auto [early_short, late_short] = tail(0.4f, nullptr);
    const auto [early_long, late_long] = tail(0.9f, &out);
    CHECK(early_long > 1e-4);
    CHECK(late_long < early_long);         // decays
    CHECK(late_long > 10.0 * late_short);  // a longer decay rings longer
    CHECK(early_short > 0.0);
    double corr = 0.0;
    double el = 0.0;
    double er = 0.0;
    for (std::size_t i = kRate / 10; i < kRate; ++i) {
        corr += static_cast<double>(out.l[i]) * out.r[i];
        el += static_cast<double>(out.l[i]) * out.l[i];
        er += static_cast<double>(out.r[i]) * out.r[i];
    }
    CHECK(std::fabs(corr / std::sqrt(el * er)) < 0.5); // wide, not mono
    for (std::size_t i = 0; i < out.l.size(); ++i) {
        REQUIRE(std::isfinite(out.l[i]));
    }
}

TEST_CASE("CHORUS and TAPE at MIX 0 pass the input unchanged; TAPE wow bends the pitch", "[vapor][fx]") {
    const auto s = sine(1000.0, kRate);
    for (const auto type : {model::FxType::Chorus, model::FxType::Tape}) {
        auto unit = dsp::make_fx_unit(type, kRate);
        model::FxSlot slot = model::make_fx(type);
        slot.p[4] = 0.0f;
        const Stereo out = run_fx(*unit, slot, Stereo{s, s});
        CHECK(out.l == s);
    }
    dsp::TapeFx tape(kRate);
    model::FxSlot slot = model::make_fx(model::FxType::Tape);
    slot.p = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const Stereo out = run_fx(tape, slot, Stereo{sine(1000.0, kRate * 3), sine(1000.0, kRate * 3)});
    double lo = 1e9;
    double hi = 0.0;
    for (std::size_t w = kRate / 2; w + kRate / 10 < out.l.size(); w += kRate / 10) {
        const double f = zero_cross_hz(out.l, w, w + kRate / 10);
        lo = std::min(lo, f);
        hi = std::max(hi, f);
    }
    CHECK(hi - lo > 5.0);  // audible wow (several Hz around 1 kHz)
    CHECK(hi - lo < 60.0); // but not a vibrato
}

TEST_CASE("DUCK pulls the level down while the source track plays", "[vapor][fx]") {
    dsp::DuckFx fx(kRate);
    model::FxSlot slot = model::make_fx(model::FxType::Duck);
    slot.p = {0.0f, 1.0f, 100.0f, -30.0f, 0.0f};
    const auto s = sine(200.0, kRate);
    Stereo side{std::vector<float>(kRate), std::vector<float>(kRate)};
    for (std::size_t i = kRate / 2; i < kRate / 2 + 2400; ++i) {
        side.l[i] = side.r[i] = 0.9f; // a loud 50 ms hit
    }
    const Stereo out = run_fx(fx, slot, Stereo{s, s}, 120.0, &side);
    CHECK(rms(out.l, kRate / 4, kRate / 2 - 100) == Catch::Approx(rms(s, kRate / 4, kRate / 2 - 100)));
    CHECK(rms(out.l, kRate / 2 + 200, kRate / 2 + 2400) < 0.1 * rms(s, kRate / 2 + 200, kRate / 2 + 2400));
    CHECK(rms(out.l, kRate * 9 / 10, kRate) > 0.9 * rms(s, kRate * 9 / 10, kRate)); // recovered
}

TEST_CASE("Every FX unit processes allocation-free and stays finite", "[vapor][fx][realtime]") {
    std::vector<std::unique_ptr<dsp::FxUnit>> units;
    std::vector<model::FxSlot> slots;
    for (int t = 1; t < model::kFxTypes; ++t) {
        units.push_back(dsp::make_fx_unit(static_cast<model::FxType>(t), kRate));
        slots.push_back(model::make_fx(static_cast<model::FxType>(t)));
    }
    CHECK(dsp::make_fx_unit(model::FxType::Off, kRate) == nullptr);
    std::vector<float> l(2048);
    std::vector<float> r(2048);
    Noise noise;
    rt::set_abort_on_violation(false);
    const auto before = rt::violations();
    {
        rt::Scope realtime;
        for (int block = 0; block < 50; ++block) {
            for (std::size_t i = 0; i < l.size(); ++i) {
                l[i] = noise.next() * 0.8f;
                r[i] = noise.next() * 0.8f;
            }
            for (std::size_t u = 0; u < units.size(); ++u) {
                const dsp::FxContext ctx{&slots[u], 97.0, l.data(), r.data()};
                units[u]->process(l.data(), r.data(), l.size(), ctx);
            }
        }
    }
    CHECK(rt::violations() == before);
    rt::set_abort_on_violation(true);
    for (std::size_t i = 0; i < l.size(); ++i) {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::fabs(l[i]) < 20.0f);
    }
}

TEST_CASE("Projects keep loops, effects, sends and VAPOR (schemaVersion 4)", "[vapor][project]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "phosphor_vapor_project_test";
    fs::remove_all(dir);
    model::Document doc;
    model::Song& s = doc.song;
    s.loops[2].name = "MALL";
    s.loops[2].sample = model::make_sample("MALL", "", kRate, sine(220.0, kRate), sine(330.0, kRate));
    s.loops[2].bars = 2;
    s.loops[2].sync = model::LoopSync::Slice;
    s.loops[2].quantize = model::LaunchQuantize::Beat;
    s.loops[2].slices = 8;
    s.loops[2].screw = 0.8f;
    s.loops[2].reverse = true;
    s.loops[2].track = 3;
    s.tracks[5].type = model::TrackType::Loops;
    s.tracks[1].inserts[0] = model::make_fx(model::FxType::Chorus);
    s.tracks[1].inserts[1] = model::make_fx(model::FxType::Crush);
    s.tracks[1].inserts[1].p[0] = 6.0f;
    s.tracks[1].send_a = 0.4f;
    s.tracks[1].send_b = 0.25f;
    s.returns[1] = model::make_fx(model::FxType::Tape);
    s.return_level = {0.5f, 0.75f};
    s.master_fx[0] = model::make_fx(model::FxType::Lofi);
    s.vapor = 70;
    std::string error;
    REQUIRE(io::save_project(dir / "V.phosphor", doc, &error));
    CHECK(fs::exists(dir / "V.phosphor" / "loops" / "03_MALL.wav"));
    const auto loaded = io::load_project(dir / "V.phosphor", kRate, &error);
    REQUIRE(loaded);
    const model::Song& l = loaded->song;
    REQUIRE(l.loops[2].sample);
    CHECK(l.loops[2].sample->left == s.loops[2].sample->left);
    CHECK(l.loops[2].sample->right == s.loops[2].sample->right);
    model::LoopSlot a = l.loops[2];
    model::LoopSlot b = s.loops[2];
    a.sample = b.sample = nullptr;
    CHECK(a == b);
    CHECK(l.tracks == s.tracks);
    CHECK(l.returns == s.returns);
    CHECK(l.return_level == s.return_level);
    CHECK(l.master_fx == s.master_fx);
    CHECK(l.vapor == 70);
    CHECK(io::project_json(*loaded) == io::project_json(doc));

    // A loop emptied before the next save leaves no stale file.
    model::Document doc2 = *loaded;
    doc2.song.loops[2] = model::LoopSlot{};
    REQUIRE(io::save_project(dir / "V.phosphor", doc2, &error));
    CHECK_FALSE(fs::exists(dir / "V.phosphor" / "loops" / "03_MALL.wav"));
    fs::remove_all(dir);

    // v3 files: NOTES tracks without effects, default returns, VAPOR 0.
    std::string v3 = R"({"schemaVersion": 3, "instruments": [], "song": {"tracks": [{"name": "T"}]}})";
    REQUIRE(io::migrate_project(v3, &error));
    CHECK(v3.find("\"schemaVersion\": 5") != std::string::npos);
}

TEST_CASE("Loop bar detection picks the power of two nearest the project tempo", "[vapor][loops]") {
    CHECK(model::detect_bars(2.0, 120.0) == 1);   // 1 bar at 120 BPM
    CHECK(model::detect_bars(8.0, 120.0) == 4);
    CHECK(model::detect_bars(10.0, 120.0) == 4);  // 5 bars -> 4 (96 BPM loop)
    CHECK(model::detect_bars(14.0, 120.0) == 8);  // 7 bars -> 8
    CHECK(model::detect_bars(0.5, 120.0) == 1);
    model::LoopSlot slot;
    slot.sample = model::make_sample("X", "", kRate, std::vector<float>(kRate * 4));
    slot.bars = 2;
    CHECK(slot.loop_bpm() == Catch::Approx(120.0)); // 8 beats in 4 s
}

// ---------------------------------------------------------------------------------------------
// Engine: loops, effects, VAPOR
// ---------------------------------------------------------------------------------------------


namespace {

// `beats` beats at `loop_bpm`: a 25 ms 1 kHz burst on every beat, silence between.
model::SampleRef burst_loop(double loop_bpm, int beats) {
    const auto beat = static_cast<std::size_t>(60.0 / loop_bpm * kRate);
    std::vector<float> v(beat * static_cast<std::size_t>(beats));
    for (int b = 0; b < beats; ++b) {
        for (std::size_t i = 0; i < kRate / 40; ++i) {
            const double env = std::sin(kPi * static_cast<double>(i) / (kRate / 40.0));
            v[static_cast<std::size_t>(b) * beat + i] =
                static_cast<float>(0.8 * env * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kRate));
        }
    }
    return model::make_sample("BURST", "", kRate, std::move(v));
}

struct Rig {
    engine::Engine eng;
    engine::SnapshotBuilder builder;
    model::Document doc;
    std::vector<float> out; // mono mix of everything rendered

    Rig() { eng.prepare(kRate, 2); }
    void publish() {
        builder.stretch_now(doc, doc.song.section().bpm10, kRate);
        eng.publish(builder.build(doc, kRate));
        render(64); // swap the snapshot in
    }
    void render(std::size_t frames) {
        std::vector<float> buf(512);
        for (std::size_t pos = 0; pos < frames; pos += 256) {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(256, frames - pos));
            eng.render_offline(buf.data(), n, 2);
            for (std::uint32_t i = 0; i < n; ++i) {
                out.push_back(0.5f * (buf[2 * i] + buf[2 * i + 1]));
            }
            eng.collect_garbage();
        }
    }
    void seconds(double s) { render(static_cast<std::size_t>(s * kRate)); }
};

// Onset frames: the signal rises above `level` after at least 50 ms below it.
std::vector<std::size_t> onsets(const std::vector<float>& v, std::size_t from, float level = 0.05f) {
    std::vector<std::size_t> o;
    std::size_t quiet = kRate;
    for (std::size_t i = from; i < v.size(); ++i) {
        if (std::fabs(v[i]) > level) {
            if (quiet >= kRate / 20) {
                o.push_back(i);
            }
            quiet = 0;
        } else {
            ++quiet;
        }
    }
    return o;
}

double burst_hz(const std::vector<float>& v, std::size_t onset) {
    return zero_cross_hz(v, onset + kRate / 400, onset + kRate / 50);
}

} // namespace

TEST_CASE("Tempo changes reflow loops in VARISPEED, STRETCH and SLICE", "[vapor][loops][ac]") {
    // PLAN.md M7 AC. A 4-beat loop recorded at 100 BPM plays at 120, then the tempo drops to 90.
    for (const auto sync : {model::LoopSync::Varispeed, model::LoopSync::Stretch, model::LoopSync::Slice}) {
        CAPTURE(model::loop_sync_name(sync));
        Rig rig;
        model::LoopSlot& slot = rig.doc.song.loops[0];
        slot.sample = burst_loop(100.0, 4);
        slot.bars = 1;
        slot.sync = sync;
        slot.slices = 4;
        slot.quantize = model::LaunchQuantize::Immediate;
        rig.doc.song.sections[0].bpm10 = 1200;
        rig.publish();
        rig.eng.send(engine::EngineCommand::loop(0, 1));
        const std::size_t start = rig.out.size();
        rig.seconds(3.0);
        auto o = onsets(rig.out, start);
        REQUIRE(o.size() >= 5);
        for (std::size_t i = 1; i < o.size(); ++i) {
            CHECK(static_cast<double>(o[i] - o[i - 1]) == Catch::Approx(0.5 * kRate).margin(kRate * 0.004));
        }
        // VARISPEED transposes with the tempo (1000 Hz x 1.2); STRETCH and SLICE keep the pitch.
        const double expect_hz = sync == model::LoopSync::Varispeed ? 1200.0 : 1000.0;
        CHECK(burst_hz(rig.out, o[2]) == Catch::Approx(expect_hz).epsilon(0.03));

        rig.doc.song.sections[0].bpm10 = 900;
        rig.publish();
        const std::size_t later = rig.out.size() + kRate; // let the new tempo settle for a beat
        rig.seconds(5.0);
        o = onsets(rig.out, later);
        REQUIRE(o.size() >= 4);
        for (std::size_t i = 1; i < o.size(); ++i) {
            CHECK(static_cast<double>(o[i] - o[i - 1]) == Catch::Approx(60.0 / 90.0 * kRate).margin(kRate * 0.004));
        }
        CHECK(burst_hz(rig.out, o[1]) == Catch::Approx(sync == model::LoopSync::Varispeed ? 900.0 : 1000.0).epsilon(0.03));
    }
}

TEST_CASE("STRETCH plays varispeed until the cache for the new tempo is ready", "[vapor][loops]") {
    Rig rig;
    model::LoopSlot& slot = rig.doc.song.loops[0];
    slot.sample = burst_loop(100.0, 4);
    slot.sync = model::LoopSync::Stretch;
    slot.quantize = model::LaunchQuantize::Immediate;
    rig.doc.song.sections[0].bpm10 = 1500;
    rig.eng.publish(rig.builder.build(rig.doc, kRate)); // no cache yet
    rig.render(64);
    rig.eng.send(engine::EngineCommand::loop(0, 1));
    const std::size_t start = rig.out.size();
    rig.seconds(1.5);
    const auto o = onsets(rig.out, start);
    REQUIRE(o.size() >= 3);
    CHECK(static_cast<double>(o[1] - o[0]) == Catch::Approx(0.4 * kRate).margin(kRate * 0.004)); // on tempo
    CHECK(burst_hz(rig.out, o[1]) == Catch::Approx(1500.0).epsilon(0.03));                         // varispeed
}

TEST_CASE("Loop launch quantize waits for the next bar or beat while the song plays", "[vapor][loops]") {
    for (const auto q : {model::LaunchQuantize::Bar, model::LaunchQuantize::Beat}) {
        Rig rig;
        model::LoopSlot& slot = rig.doc.song.loops[0];
        slot.sample = burst_loop(120.0, 4);
        slot.quantize = q;
        rig.doc.song.sections[0].bpm10 = 1200; // 16 x 1/16 = one 2 s bar
        rig.doc.song.metronome = model::MetronomeMode::Off;
        rig.publish();
        const std::size_t play_at = rig.out.size();
        rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::Play));
        rig.seconds(0.3);
        rig.eng.send(engine::EngineCommand::loop(0, 1));
        rig.seconds(2.5);
        const auto o = onsets(rig.out, play_at);
        REQUIRE_FALSE(o.empty());
        const double expect = q == model::LaunchQuantize::Bar ? 2.0 : 0.5; // next bar / next beat after 0.3 s
        CHECK(static_cast<double>(o[0] - play_at) / kRate == Catch::Approx(expect).margin(0.01));
    }
}

TEST_CASE("A LOOPS track launches slots for as long as its notes last", "[vapor][loops]") {
    Rig rig;
    model::Song& s = rig.doc.song;
    s.loops[1].sample = burst_loop(120.0, 4);
    s.tracks[0].type = model::TrackType::Loops;
    s.metronome = model::MetronomeMode::Off;
    s.sections[0].bpm10 = 1200;
    // Note D4 (slot 2) for the first half of the bar.
    model::insert_note(s.patterns[0].lanes[0], model::Note{0, model::kPpq * 2, model::kLoopBaseNote + 1, 100});
    rig.publish();
    const std::size_t play_at = rig.out.size();
    rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::Play));
    rig.seconds(3.9);
    const auto o = onsets(rig.out, play_at);
    // Beats 1 and 2 of each bar, then silence until the next pass: onsets at 0, 0.5, 2.0, 2.5 s.
    for (const auto x : o) {
        UNSCOPED_INFO("onset " << static_cast<double>(x - play_at) / kRate);
    }
    REQUIRE(o.size() == 4);
    const std::array<double, 4> expect = {0.0, 0.5, 2.0, 2.5};
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(static_cast<double>(o[i] - play_at) / kRate == Catch::Approx(expect[i]).margin(0.01));
    }
}

TEST_CASE("Sends feed the returns; inserts and VAPOR change the track and master", "[vapor][fx][engine]") {
    const auto tail_rms = [](bool send, int vapor) {
        Rig rig;
        model::Song& s = rig.doc.song;
        s.loops[0].sample = burst_loop(120.0, 4);
        s.loops[0].quantize = model::LaunchQuantize::Immediate;
        s.loops[0].track = 8;
        s.tracks[8].send_a = send ? 1.0f : 0.0f;
        s.returns[0].p[0] = 0.9f; // long plate
        s.vapor = vapor;
        rig.publish();
        rig.eng.send(engine::EngineCommand::loop(0, 1));
        rig.seconds(0.1);
        rig.eng.send(engine::EngineCommand::loop(0, 0)); // one burst, then stop
        rig.seconds(1.0);
        return rms(rig.out, rig.out.size() - kRate / 2, rig.out.size());
    };
    CHECK(tail_rms(false, 0) < 1e-6);   // dry: silence after the burst
    CHECK(tail_rms(true, 0) > 1e-4);    // plate return rings on
    CHECK(tail_rms(false, 100) > 1e-4); // VAPOR adds its own plate (and hiss)
}

TEST_CASE("Loops, track FX, returns and VAPOR render allocation-free", "[vapor][realtime]") {
    if (!rt::guard_enabled()) {
        SUCCEED("allocation guard only in Debug builds");
        return;
    }
    Rig rig;
    model::Song& s = rig.doc.song;
    for (int i = 0; i < model::kLoopSlots; ++i) {
        s.loops[static_cast<std::size_t>(i)].sample = burst_loop(110.0, 4);
        s.loops[static_cast<std::size_t>(i)].sync = static_cast<model::LoopSync>(i % 3);
        s.loops[static_cast<std::size_t>(i)].quantize = model::LaunchQuantize::Immediate;
        s.loops[static_cast<std::size_t>(i)].lp_hz = 3000.0f;
    }
    for (int t = 0; t < model::kTracks; ++t) {
        auto& tr = s.tracks[static_cast<std::size_t>(t)];
        tr.inserts[0] = model::make_fx(static_cast<model::FxType>(1 + t % (model::kFxTypes - 1)));
        tr.inserts[1] = model::make_fx(static_cast<model::FxType>(1 + (t + 3) % (model::kFxTypes - 1)));
        tr.send_a = tr.send_b = 0.3f;
    }
    s.master_fx = {model::make_fx(model::FxType::Chorus), model::make_fx(model::FxType::Crush)};
    s.vapor = 60;
    rig.publish();
    for (int i = 0; i < model::kLoopSlots; ++i) {
        rig.eng.send(engine::EngineCommand::loop(i, 1));
    }
    rt::set_abort_on_violation(false);
    const auto before = rt::violations();
    rig.seconds(1.0); // render_offline runs inside the engine's realtime scope
    CHECK(rt::violations() == before);
    rt::set_abort_on_violation(true);
    for (const float x : rig.out) {
        REQUIRE(std::isfinite(x));
    }
}

TEST_CASE("NEW MALLSOFT plays immediately; every template makes sound", "[vapor][templates][ac]") {
    // PLAN.md M7 AC: after NEW MALLSOFT, PLAY gives music at once (no import, no waiting).
    for (const auto name : content::template_names()) {
        CAPTURE(std::string(name));
        const auto t0 = std::chrono::steady_clock::now();
        auto doc = content::make_template(name, kRate);
        const double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("template %.*s built in %.3f s\n", static_cast<int>(name.size()), name.data(), build_s);
        REQUIRE(doc);
        REQUIRE(doc->song.loops[0].sample);
        CHECK(doc->song.loops[0].sample->frames > kRate);
        Rig rig;
        rig.doc = *doc;
        rig.publish();
        const std::size_t play_at = rig.out.size();
        rig.eng.send(engine::EngineCommand::transport(engine::TransportAction::Play));
        rig.seconds(0.25);
        const double first = rms(rig.out, play_at, rig.out.size());
        CHECK(20.0 * std::log10(first) > -40.0); // sound within the first quarter second
        rig.seconds(8.0);                         // several passes: the loop relaunches every pass
        const auto info = rig.eng.read_loops();
        CHECK(info.state[0] == engine::LoopState::Playing);
        const double late = rms(rig.out, rig.out.size() - kRate, rig.out.size());
        CHECK(20.0 * std::log10(late) > -40.0);
        float peak = 0.0f;
        for (const float x : rig.out) {
            REQUIRE(std::isfinite(x));
            peak = std::max(peak, std::fabs(x));
        }
        CHECK(peak < 1.0f); // the limiter keeps it below full scale
    }
    CHECK_FALSE(content::make_template("POLKA", kRate));
    CHECK(content::make_template("future funk", kRate)); // spaces and case do not matter
}

// Writes tests/fixtures/vapor.phosphor (Page L / M goldens). Run on purpose only:
//   phosphor_tests "[.fixture]"
TEST_CASE("make the vapor fixture", "[.fixture]") {
    auto doc = content::make_template("MALLSOFT", 24000);
    REQUIRE(doc);
    model::Song& s = doc->song;
    s.loops[1] = s.loops[0];
    s.loops[1].sample = nullptr;
    s.loops[1].name = "EMPTY";
    s.loops[2] = model::LoopSlot{};
    s.loops[2].name = "EMPTY";
    s.loops[0].sync = model::LoopSync::Slice;
    s.loops[0].slices = 8;
    s.tracks[0].inserts = {model::make_fx(model::FxType::Chorus), model::make_fx(model::FxType::Tape)};
    s.tracks[0].send_b = 0.25f;
    s.master_fx[0] = model::make_fx(model::FxType::Lofi);
    std::string error;
    const auto dir = std::filesystem::path(PHOS_FIXTURE_DIR) / "vapor.phosphor";
    std::filesystem::remove_all(dir);
    REQUIRE(io::save_project(dir, *doc, &error));
}

TEST_CASE("Autosaves keep their own audio; the saved project's audio stays untouched", "[vapor][project]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "phosphor_autosave_audio_test";
    fs::remove_all(dir);
    const fs::path bundle = dir / "A.phosphor";
    model::Document doc;
    doc.instruments[0].name = "ONE";
    doc.instruments[0].sample = model::make_sample("ONE", "", kRate, sine(220.0, 4800));
    doc.instruments[1] = model::make_instrument(model::InstrumentKind::Wavetable, "WAVE");
    doc.song.loops[0].name = "LOOP";
    doc.song.loops[0].sample = model::make_sample("LOOP", "", kRate, sine(110.0, 9600));
    std::string error;
    REQUIRE(io::save_project(bundle, doc, &error));

    // Edited but not saved: the autosave must not touch what project.json uses.
    model::Document edited = doc;
    edited.instruments[0].sample = model::make_sample("ONE", "", kRate, sine(880.0, 4800));
    edited.song.loops[0].sample = model::make_sample("LOOP", "", kRate, sine(440.0, 9600));
    auto wt = std::make_shared<model::WavetableData>(*edited.instruments[1].wavetable);
    wt->segments[0].wave.fill(0.25f);
    edited.instruments[1].wavetable = wt;
    REQUIRE(io::save_project(bundle, edited, &error, io::kAutosaveFile));
    CHECK(fs::exists(bundle / "autosave"));

    const auto saved = io::load_project(bundle, kRate, &error);
    REQUIRE(saved);
    CHECK(saved->instruments[0].sample->left == doc.instruments[0].sample->left);
    CHECK(saved->song.loops[0].sample->left == doc.song.loops[0].sample->left);
    CHECK(*saved->instruments[1].wavetable == *doc.instruments[1].wavetable);
    const auto recovered = io::load_project(bundle, kRate, &error, io::kAutosaveFile);
    REQUIRE(recovered);
    CHECK(recovered->instruments[0].sample->left == edited.instruments[0].sample->left);
    CHECK(recovered->song.loops[0].sample->left == edited.song.loops[0].sample->left);
    CHECK(*recovered->instruments[1].wavetable == *wt);

    // A real save takes over the edits and removes the autosave with its audio.
    REQUIRE(io::save_project(bundle, *recovered, &error));
    CHECK_FALSE(fs::exists(bundle / "autosave"));
    CHECK_FALSE(fs::exists(bundle / io::kAutosaveFile));
    const auto again = io::load_project(bundle, kRate, &error);
    REQUIRE(again);
    CHECK(again->instruments[0].sample->left == edited.instruments[0].sample->left);
    fs::remove_all(dir);
}
