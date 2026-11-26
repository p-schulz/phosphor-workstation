#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "core/midi.h"
#include "core/rt_guard.h"
#include "core/time_dll.h"
#include "engine/engine.h"

using namespace phos;
using namespace phos::engine;
using Catch::Approx;

namespace {

std::vector<midi::Message> parse(const std::vector<std::uint8_t>& bytes, midi::Parser& p) {
    std::vector<midi::Message> out;
    p.feed(bytes.data(), bytes.size(), [&](const midi::Message& m) { out.push_back(m); });
    return out;
}

constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kBlock = 64;
constexpr double kPeriodNs = 1e9 * kBlock / kRate;
constexpr std::int64_t kT0 = 5'000'000'000;

// Drives an engine with a fake device clock: block k starts at kT0 + k * period.
struct Clocked {
    std::unique_ptr<Engine> e = std::make_unique<Engine>();
    std::vector<float> out = std::vector<float>(kBlock * 2);
    long block = 0;

    Clocked() { e->prepare(kRate, 2); }
    std::int64_t now() const { return kT0 + static_cast<std::int64_t>(static_cast<double>(block) * kPeriodNs); }
    void run(int blocks) {
        for (int i = 0; i < blocks; ++i) {
            e->process(out.data(), kBlock, 2, now());
            ++block;
        }
    }
    void publish(Placement placement) {
        auto s = std::make_unique<EngineSnapshot>();
        s->placement = placement;
        e->publish(std::move(s));
        run(1);
        e->collect_garbage();
    }
    void send(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::int64_t ns, int slot = 0) {
        const std::uint8_t bytes[3] = {a, b, c};
        e->midi_queues().push_bytes(slot, bytes, 3, ns);
    }
    int first_sound_offset() const {
        for (std::size_t i = 0; i < kBlock; ++i) {
            if (out[i * 2] != 0.0f) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

} // namespace

TEST_CASE("midi parser handles running status, realtime bytes and sysex", "[midi]") {
    midi::Parser p;
    // Note on with running status, an active-sensing byte in the middle of the second message.
    auto m = parse({0x90, 60, 100, 62, 0xFE, 90, 64, 0}, p);
    REQUIRE(m.size() == 3);
    CHECK(m[0].is_note_on());
    CHECK(m[1].data1 == 62);
    CHECK(m[1].data2 == 90);
    CHECK(m[2].is_note_off()); // note on with velocity 0
    // SysEx is skipped and cancels running status; program change has one data byte.
    m = parse({0xF0, 0x7E, 0x01, 0xF7, 70, 0xC3, 5, 0xB0, 64, 127}, p);
    REQUIRE(m.size() == 2);
    CHECK(m[0].type() == midi::kProgramChange);
    CHECK(m[0].channel() == 3);
    CHECK(m[0].data1 == 5);
    CHECK(m[1].type() == midi::kControlChange);
    // A message split across two packets.
    CHECK(parse({0xE0, 0x00}, p).empty());
    m = parse({0x40}, p);
    REQUIRE(m.size() == 1);
    CHECK(m[0].type() == midi::kPitchBend);
    CHECK((m[0].data2 << 7 | m[0].data1) == 8192);
}

TEST_CASE("time DLL locks to a jittery callback clock and tracks its rate", "[midi]") {
    TimeDll dll;
    dll.reset(kRate, 1.0);
    std::uint64_t frame = 0;
    // The device runs 50 ppm fast relative to the host clock; callbacks jitter by +-200 us.
    const double true_ns_per_frame = 1e9 / kRate * (1.0 - 50e-6);
    for (int i = 0; i < 20000; ++i) {
        const double t = kT0 + static_cast<double>(frame) * true_ns_per_frame;
        const double jitter = ((i * 7919) % 401 - 200) * 1000.0;
        dll.update(static_cast<std::int64_t>(t + jitter), frame, kBlock);
        frame += kBlock;
    }
    REQUIRE(dll.locked());
    // The 50 ppm offset is learned to within 25 ppm despite +-200 us jitter (nominal is 50 ppm off).
    CHECK(std::fabs(dll.ns_per_frame() / true_ns_per_frame - 1.0) < 25e-6);
    const double truth = kT0 + static_cast<double>(frame) * true_ns_per_frame;
    CHECK(std::fabs(dll.frame_to_ns(static_cast<double>(frame)) - truth) < 60'000.0); // < 60 us
    CHECK(dll.ns_to_frame(dll.frame_to_ns(1234.0)) == Approx(1234.0).margin(1e-6));

    // A jump (device restart) re-synchronises instead of slewing for seconds.
    dll.update(static_cast<std::int64_t>(truth + 100e6), frame, kBlock);
    CHECK_FALSE(dll.locked());
}

TEST_CASE("IMMEDIATE places events at the block start, STABLE at timestamp plus one block", "[midi]") {
    Clocked c;
    c.run(40); // lock the DLL

    // IMMEDIATE: an event from the middle of the previous block sounds from the start of this one.
    c.send(0x90, 69, 127, c.now() - static_cast<std::int64_t>(kPeriodNs / 2));
    c.run(1);
    CHECK(c.first_sound_offset() >= 0);
    CHECK(c.first_sound_offset() <= 1);
    c.send(0x80, 69, 0, c.now());
    c.run(200);

    // STABLE: an event 20 frames into the previous block sounds 20 frames into this block.
    c.publish(Placement::Stable);
    c.run(5);
    const auto ts = c.now() - static_cast<std::int64_t>(kPeriodNs) + static_cast<std::int64_t>(20 * 1e9 / kRate);
    c.send(0x90, 69, 127, ts);
    c.run(1);
    const int onset = c.first_sound_offset();
    CHECK(onset >= 20);
    CHECK(onset <= 22); // the sine starts at phase 0: the first sample after the onset is non-zero
    const auto l = c.e->latency();
    CHECK(l.max_ms == Approx(kPeriodNs / 1e6).margin(0.03)); // constant one-block delay
}

TEST_CASE("a burst through the input queues loses no notes", "[midi]") {
    Clocked c;
    c.run(40);
    // 8000 note-on/off pairs pushed before the audio thread runs (16 000 events, queue 16 384),
    // as one packet stream with running status.
    std::vector<std::uint8_t> bytes = {0x90};
    for (int i = 0; i < 8000; ++i) {
        const auto n = static_cast<std::uint8_t>(36 + i % 48);
        bytes.insert(bytes.end(), {n, 100, n, 0});
    }
    c.e->midi_queues().push_bytes(0, bytes.data(), bytes.size(), c.now());
    c.run(10);
    const auto m = c.e->read_midi_meters();
    CHECK(m.note_ons == 8000);
    CHECK(m.events == 16000);
    CHECK(m.queue_overflow == 0);
    CHECK(m.schedule_overflow == 0);
    CHECK(m.held == std::array<std::uint64_t, 2>{0, 0});

    // Beyond the queue capacity, the overflow is counted rather than hidden.
    std::vector<std::uint8_t> flood = {0x90};
    for (std::size_t i = 0; i < MidiInputQueues::kCapacity + 10; ++i) {
        flood.insert(flood.end(), {60, 100});
    }
    c.e->midi_queues().push_bytes(1, flood.data(), flood.size(), c.now());
    CHECK(c.e->read_midi_meters().queue_overflow == 10);
}

TEST_CASE("sustain pedal holds released notes until it goes up", "[midi]") {
    Clocked c;
    c.run(20);
    c.send(0xB0, 64, 127, c.now()); // pedal down
    c.send(0x90, 60, 100, c.now());
    c.run(10);
    c.send(0x80, 60, 0, c.now());
    c.run(200); // 266 ms, far past the 60 ms release
    CHECK(c.e->read_meters().voices == 1);
    CHECK_FALSE(c.e->read_midi_meters().held_note(60)); // the key is up, the voice still sounds
    c.send(0xB0, 64, 0, c.now()); // pedal up
    c.run(200);
    CHECK(c.e->read_meters().voices == 0);
}

TEST_CASE("pitch bend shifts the test voice by the bend range", "[midi]") {
    Engine e;
    e.prepare(kRate, 2);
    e.send(EngineCommand::midi_message(0xE0, 0x7F, 0x7F, 0)); // full up: +2 semitones (default range)
    e.send(EngineCommand::midi_message(0x90, 69, 127, 0));
    std::vector<float> out(kRate * 2);
    e.render_offline(out.data(), kRate, 2);
    int crossings = 0;
    for (std::size_t i = 4801; i < kRate; ++i) {
        crossings += (out[(i - 1) * 2] < 0.0f && out[i * 2] >= 0.0f) ? 1 : 0;
    }
    const double hz = crossings * static_cast<double>(kRate) / (kRate - 4800);
    CHECK(hz == Approx(440.0 * std::exp2((2.0 * 8191.0 / 8192.0) / 12.0)).margin(1.5));
}

TEST_CASE("channel filter, CC binding and the CC monitor", "[midi]") {
    Clocked c;
    auto s = std::make_unique<EngineSnapshot>();
    s->slot_channel[0] = 0; // slot 0 listens to channel 1 only
    s->cc_bindings[0] = CcBinding{7, 0xFF, EngineParam::MasterGainDb};
    s->cc_binding_count = 1;
    c.e->publish(std::move(s));
    c.run(20);

    c.send(0x91, 60, 100, c.now()); // channel 2: filtered out
    c.run(2);
    CHECK(c.e->read_midi_meters().note_ons == 0);
    c.send(0x90, 60, 100, c.now()); // channel 1
    c.run(20);
    CHECK(c.e->read_midi_meters().note_ons == 1);
    CHECK(c.e->read_midi_meters().held_note(60));

    c.send(0xB0, 7, 0, c.now()); // CC 7 -> MASTER -60 dB
    c.run(100);
    float peak = 0.0f;
    for (const float v : c.out) {
        peak = std::max(peak, std::fabs(v));
    }
    CHECK(peak < 0.001f);
    CcEvent cc;
    REQUIRE(c.e->pop_cc(cc));
    CHECK(cc.cc == 7);
    CHECK(cc.value == 0);
    CHECK(cc.channel == 0);
}

TEST_CASE("fixed velocity curve ignores how hard a key is hit", "[midi]") {
    auto level = [](VelocityCurve curve, std::uint8_t velocity) {
        Engine e;
        auto s = std::make_unique<EngineSnapshot>();
        s->velocity_curve = curve;
        e.publish(std::move(s));
        e.send(EngineCommand::midi_message(0x90, 69, velocity, 0));
        std::vector<float> out(4800 * 2);
        e.render_offline(out.data(), 4800, 2);
        float peak = 0.0f;
        for (const float v : out) {
            peak = std::max(peak, std::fabs(v));
        }
        return peak;
    };
    CHECK(level(VelocityCurve::Fixed, 10) == Approx(level(VelocityCurve::Fixed, 127)));
    CHECK(level(VelocityCurve::Linear, 64) == Approx(level(VelocityCurve::Linear, 127) * 64.0 / 127.0).epsilon(0.01));
    CHECK(level(VelocityCurve::Hard, 64) < level(VelocityCurve::Linear, 64));
    CHECK(level(VelocityCurve::Soft, 64) > level(VelocityCurve::Linear, 64));
}

TEST_CASE("process stays allocation-free with MIDI input and STABLE scheduling", "[midi][realtime]") {
    if (!rt::guard_enabled()) {
        SKIP("allocation guard is compiled in Debug builds only");
    }
    rt::set_abort_on_violation(false);
    Clocked c;
    c.publish(Placement::Stable);
    const auto before = rt::violations();
    for (int i = 0; i < 3000; ++i) {
        const auto n = static_cast<std::uint8_t>(40 + i % 40);
        c.send(0x90, n, 100, c.now() - 300'000);
        c.send(0xE0, 0, static_cast<std::uint8_t>(i % 128), c.now());
        c.send(0xB0, static_cast<std::uint8_t>(i % 2 ? 1 : 64), static_cast<std::uint8_t>(i % 128), c.now());
        c.send(0x80, n, 0, c.now() - 100'000);
        c.run(1);
        CcEvent cc;
        while (c.e->pop_cc(cc)) {
        }
    }
    CHECK(rt::violations() == before);
    rt::set_abort_on_violation(true);
}
