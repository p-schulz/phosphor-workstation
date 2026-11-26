#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "core/rt_guard.h"
#include "dsp/analysis.h"
#include "dsp/smoother.h"
#include "engine/engine.h"

using namespace phos;
using namespace phos::engine;
using Catch::Approx;

namespace {

constexpr std::uint32_t kRate = 48000;

// Renders `frames` stereo frames in blocks of `block`.
std::vector<float> render(Engine& e, std::size_t frames, std::uint32_t block = 64) {
    std::vector<float> out(frames * 2);
    for (std::size_t pos = 0; pos < frames; pos += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, frames - pos));
        e.render_offline(out.data() + pos * 2, n, 2);
    }
    return out;
}

float peak(const std::vector<float>& v, std::size_t from = 0) {
    float p = 0.0f;
    for (std::size_t i = from; i < v.size(); ++i) {
        p = std::max(p, std::fabs(v[i]));
    }
    return p;
}

// Rising zero crossings per second on the left channel.
double measured_hz(const std::vector<float>& v, std::size_t skip_frames) {
    int crossings = 0;
    const std::size_t frames = v.size() / 2;
    for (std::size_t i = skip_frames + 1; i < frames; ++i) {
        if (v[(i - 1) * 2] < 0.0f && v[i * 2] >= 0.0f) {
            ++crossings;
        }
    }
    return crossings * static_cast<double>(kRate) / static_cast<double>(frames - skip_frames);
}

} // namespace

TEST_CASE("engine is silent without voices", "[engine]") {
    Engine e;
    e.prepare(kRate, 2);
    const auto out = render(e, 4800);
    CHECK(peak(out) == 0.0f);
    CHECK(e.read_meters().voices == 0);
}

TEST_CASE("sine test voice plays at the note frequency and releases to silence", "[engine]") {
    Engine e;
    e.prepare(kRate, 2);
    e.send(EngineCommand::note_on(TestWave::Sine, 69, 1.0f));
    auto out = render(e, kRate);
    CHECK(measured_hz(out, 4800) == Approx(440.0).margin(1.5));
    CHECK(e.read_meters().voices == 1);
    CHECK(peak(out) > 0.2f);

    e.send(EngineCommand::note_off(69));
    out = render(e, kRate / 2); // release 60 ms
    CHECK(peak(out, (kRate / 4) * 2) == 0.0f);
    CHECK(e.read_meters().voices == 0);
}

TEST_CASE("note frequencies follow equal temperament", "[engine]") {
    CHECK(VoicePool::note_hz(69) == Approx(440.0));
    CHECK(VoicePool::note_hz(60) == Approx(261.6256).margin(1e-3));
    CHECK(VoicePool::note_hz(81) == Approx(880.0));
}

TEST_CASE("click voice ends by itself", "[engine]") {
    Engine e;
    e.prepare(kRate, 2);
    e.send(EngineCommand::note_on(TestWave::Click, 0, 1.0f));
    const auto out = render(e, kRate / 10);
    CHECK(peak(out) > 0.1f);
    CHECK(e.read_meters().voices == 0);
    CHECK(peak(out, (kRate / 20) * 2) == 0.0f); // gone after 50 ms
}

TEST_CASE("voice pool steals the released voice first, then the quietest, then the oldest", "[engine]") {
    VoicePool pool;
    pool.prepare(48000.0f);
    pool.set_limit(3);
    std::vector<float> l(512);
    std::vector<float> r(512);
    pool.note_on(TestWave::Sine, 60, 1.0f);
    pool.note_on(TestWave::Sine, 62, 0.2f); // quietest
    pool.note_on(TestWave::Sine, 64, 1.0f);
    pool.render(l.data(), r.data(), 512);   // past the attack
    pool.note_off(64);                      // released

    pool.note_on(TestWave::Sine, 65, 1.0f); // takes 64's slot
    CHECK(pool.active() == 3);
    CHECK(pool.steals() == 1);
    pool.render(l.data(), r.data(), 512);
    pool.note_on(TestWave::Sine, 67, 1.0f); // no released voice: takes the quietest (62)
    pool.render(l.data(), r.data(), 512);
    pool.note_off(62); // already stolen: nothing left to release
    pool.render(l.data(), r.data(), 512);
    CHECK(pool.active() == 3);
    CHECK(pool.steals() == 2);
}

TEST_CASE("32 full-velocity voices are held at the limiter ceiling", "[engine]") {
    Engine e;
    e.prepare(kRate, 2);
    for (int n = 0; n < 32; ++n) {
        e.send(EngineCommand::note_on(TestWave::Sine, 48 + n, 1.0f));
    }
    const auto out = render(e, kRate);
    const auto m = e.read_meters();
    CHECK(m.voices == 32);
    CHECK(peak(out) <= dsp::db_to_gain(-1.0f) + 1e-6f);
    CHECK(m.gain_reduction_db < -3.0f);
    CHECK(m.peak_l <= dsp::db_to_gain(-1.0f) + 1e-6f);
}

TEST_CASE("master gain changes are smoothed, not stepped", "[engine]") {
    Engine e;
    e.prepare(kRate, 2);
    e.send(EngineCommand::note_on(TestWave::Sine, 69, 1.0f));
    render(e, kRate / 10);
    e.send(EngineCommand::set_param(EngineParam::MasterGainDb, -60.0f));
    const auto out = render(e, kRate / 10);
    // The first 64 frames after the change still carry most of the level (5 ms smoothing).
    CHECK(peak(std::vector<float>(out.begin(), out.begin() + 128)) > 0.15f);
    CHECK(peak(out, (kRate / 20) * 2) < 0.001f);
}

TEST_CASE("snapshots are swapped in on the audio side and freed on the UI side", "[engine]") {
    Engine e;
    e.prepare(kRate, 2);
    for (int i = 0; i < 5; ++i) {
        auto s = std::make_unique<EngineSnapshot>();
        s->serial = static_cast<std::uint64_t>(i + 1);
        s->limiter_ceiling_db = -6.0f;
        REQUIRE(e.publish(std::move(s)));
        render(e, 64, 64); // one block per snapshot
    }
    CHECK(e.collect_garbage() == 5); // the initial snapshot + four replaced ones
    CHECK(e.collect_garbage() == 0);

    // The last snapshot is in effect: ceiling -6 dB.
    for (int n = 0; n < 16; ++n) {
        e.send(EngineCommand::note_on(TestWave::Sine, 50 + n, 1.0f));
    }
    const auto out = render(e, kRate / 4);
    CHECK(peak(out) <= dsp::db_to_gain(-6.0f) + 1e-6f);

    // A full queue refuses further snapshots instead of blocking.
    int accepted = 0;
    for (int i = 0; i < 40; ++i) {
        accepted += e.publish(std::make_unique<EngineSnapshot>()) ? 1 : 0;
    }
    CHECK(accepted == static_cast<int>(Engine::kSnapshotCapacity));
}

TEST_CASE("blocks larger than the internal maximum and odd channel counts render correctly", "[engine]") {
    Engine e;
    e.prepare(kRate, 1);
    e.send(EngineCommand::note_on(TestWave::Sine, 69, 1.0f));
    std::vector<float> mono(5000);
    e.render_offline(mono.data(), 5000, 1);
    CHECK(peak(std::vector<float>(mono.begin() + 2500, mono.end())) > 0.2f);

    std::vector<float> quad(4 * 256, 1.0f);
    e.render_offline(quad.data(), 256, 4);
    for (std::size_t i = 0; i < 256; ++i) {
        CHECK(quad[i * 4 + 2] == 0.0f);
        CHECK(quad[i * 4 + 3] == 0.0f);
    }
}

TEST_CASE("process does not allocate, even with notes, parameters and snapshots in flight", "[engine][realtime]") {
    if (!rt::guard_enabled()) {
        SKIP("allocation guard is compiled in Debug builds only");
    }
    rt::set_abort_on_violation(false);
    const auto before = rt::violations();
    Engine e;
    e.prepare(kRate, 2);
    std::vector<float> out(Engine::kMaxBlock * 3 * 2);
    for (int block = 0; block < 2000; ++block) {
        if (block % 10 == 0) {
            e.send(EngineCommand::note_on(block % 20 == 0 ? TestWave::Click : TestWave::Sine, 40 + block % 50, 0.7f));
        }
        if (block % 13 == 0) {
            e.send(EngineCommand::note_off(40 + (block - 10) % 50));
        }
        if (block % 7 == 0) {
            e.send(EngineCommand::set_param(EngineParam::MasterGainDb, static_cast<float>(-(block % 24))));
        }
        if (block % 50 == 0) {
            e.publish(std::make_unique<EngineSnapshot>());
        }
        const auto frames = static_cast<std::uint32_t>(block % 3 == 0 ? 64 : block % 3 == 1 ? 333 : 5000);
        e.process(out.data(), std::min<std::uint32_t>(frames, Engine::kMaxBlock * 3), 2,
                  1'000'000'000LL + block * 1'000'000LL);
        e.collect_garbage();
    }
    CHECK(rt::violations() == before);
    rt::set_abort_on_violation(true);
}
