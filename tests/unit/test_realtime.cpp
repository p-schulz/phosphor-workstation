#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <new>
#include <thread>

#include "core/denormals.h"
#include "core/rt_guard.h"
#include "core/spsc_ring.h"
#include "engine/xrun_detector.h"

using namespace phos;

TEST_CASE("spsc ring keeps order, reports full and empty", "[realtime]") {
    SpscRing<int, 4> ring;
    int v = 0;
    CHECK_FALSE(ring.pop(v));
    CHECK(ring.free_space() == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(ring.push(i));
    }
    CHECK_FALSE(ring.push(99));
    CHECK(ring.free_space() == 0);
    for (int round = 0; round < 10; ++round) { // wrap around many times
        REQUIRE(ring.pop(v));
        CHECK(v == round);
        REQUIRE(ring.push(round + 4));
    }
}

TEST_CASE("spsc ring transfers a million values between two threads in order", "[realtime]") {
    SpscRing<std::uint32_t, 1024> ring;
    constexpr std::uint32_t kCount = 1'000'000;
    std::thread producer([&] {
        for (std::uint32_t i = 0; i < kCount;) {
            if (ring.push(i)) {
                ++i;
            }
        }
    });
    std::uint32_t expected = 0;
    bool in_order = true;
    while (expected < kCount) {
        std::uint32_t v = 0;
        if (ring.pop(v)) {
            in_order = in_order && v == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK(in_order);
    CHECK(ring.empty());
}

TEST_CASE("rt guard counts allocations inside a realtime scope only", "[realtime]") {
    if (!rt::guard_enabled()) {
        SKIP("allocation guard is compiled in Debug builds only");
    }
    rt::set_abort_on_violation(false);
    const auto before = rt::violations();

    void* outside = ::operator new(32);
    ::operator delete(outside);
    CHECK(rt::violations() == before);

    {
        rt::Scope scope;
        CHECK(rt::in_realtime());
        void* inside = ::operator new(32); // allocation: violation
        ::operator delete(inside);         // deallocation: violation
    }
    CHECK_FALSE(rt::in_realtime());
    CHECK(rt::violations() == before + 2);
    rt::set_abort_on_violation(true);
}

TEST_CASE("flush-to-zero turns denormal results into zero", "[realtime]") {
    if (!ScopedFlushDenormals::supported()) {
        SKIP("no FTZ control on this CPU");
    }
    volatile float tiny = std::numeric_limits<float>::min(); // smallest normal
    volatile float scale = 0.5f;
    const float without = tiny * scale;
    CHECK(without != 0.0f); // denormal
    float with = 1.0f;
    {
        ScopedFlushDenormals ftz;
        with = tiny * scale;
    }
    CHECK(with == 0.0f);
    const float after = tiny * scale; // mode restored
    CHECK(after != 0.0f);
}

TEST_CASE("xrun detector ignores jitter and counts late callbacks", "[realtime]") {
    engine::XrunDetector d;
    d.reset(48000);
    constexpr std::uint32_t kFrames = 64;
    const double period_ns = 1e9 * kFrames / 48000.0; // 1.333 ms
    double t = 1e9;
    auto tick = [&](double jitter_ns) { return d.on_callback(static_cast<std::int64_t>(t + jitter_ns), kFrames); };

    // Bursts inside the 0.5 s warm-up are ignored (300 periods = 0.4 s).
    for (int i = 0; i < 300; ++i, t += period_ns) {
        tick(i % 7 == 0 ? 3e6 : 0.0);
    }
    REQUIRE(d.xruns() == 0);
    // Steady state with ±0.4 ms jitter: no xruns.
    for (int i = 0; i < 10000; ++i, t += period_ns) {
        tick((i % 3 - 1) * 4e5);
    }
    CHECK(d.xruns() == 0);
    // One callback 5 ms late: exactly one xrun, and it re-synchronises.
    t += 5e6;
    CHECK(tick(0.0));
    t += period_ns;
    for (int i = 0; i < 1000; ++i, t += period_ns) {
        tick(0.0);
    }
    CHECK(d.xruns() == 1);
    // Clock drift of 100 ppm between audio and host clock is absorbed.
    for (int i = 0; i < 200000; ++i, t += period_ns * 1.0001) {
        tick(0.0);
    }
    CHECK(d.xruns() == 1);
}
