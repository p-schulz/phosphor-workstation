#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "model/sample_data.h"
#include "model/wave_slices.h"

using namespace phos;
using namespace phos::model;
using Catch::Approx;

namespace {

WaveSegment constant_segment(float v, int cycles = 4) {
    WaveSegment s;
    s.wave.fill(v);
    s.cycles = cycles;
    return s;
}

} // namespace

TEST_CASE("128 equal segments map one to one onto the slices", "[slices]") {
    WavetableData t;
    for (int s = 0; s < 128; ++s) {
        t.segments.push_back(constant_segment(static_cast<float>(s) / 128.0f));
    }
    const auto sl = slices::from_wavetable(t, WavetableParams{});
    REQUIRE(sl.rows.size() == 128);
    for (std::size_t k = 0; k < 128; ++k) {
        CHECK(sl.rows[k][17] == Approx(static_cast<float>(k) / 128.0f));
        CHECK(sl.keyframe[k]);
    }
}

TEST_CASE("a short wavetable is filled in as the engine crossfades it", "[slices]") {
    WavetableData t;
    t.segments = {constant_segment(0.0f), constant_segment(1.0f)}; // 4 + 4 cycles
    WavetableParams w;
    SECTION("crossfade, no loop: segment 0 fades to 1, the last one holds") {
        const auto sl = slices::from_wavetable(t, w);
        CHECK(sl.rows[0][0] == Approx(0.0f));
        CHECK(sl.rows[32][0] == Approx(0.5f));
        CHECK(sl.rows[64][0] == Approx(1.0f));
        CHECK(sl.rows[127][0] == Approx(1.0f));
        int keys = 0;
        for (std::size_t k = 0; k < 128; ++k) {
            keys += sl.keyframe[k] ? 1 : 0;
        }
        CHECK(keys == 2);
        CHECK(sl.keyframe[0]);
        CHECK(sl.keyframe[64]);
    }
    SECTION("the last loop segment fades back to the loop start") {
        w.loop_start = 0;
        w.loop_length = 2;
        const auto sl = slices::from_wavetable(t, w);
        CHECK(sl.rows[96][0] == Approx(0.5f));
    }
    SECTION("without crossfade each segment holds") {
        w.crossfade = false;
        const auto sl = slices::from_wavetable(t, w);
        CHECK(sl.rows[32][0] == Approx(0.0f));
        CHECK(sl.rows[63][0] == Approx(0.0f));
        CHECK(sl.rows[64][0] == Approx(1.0f));
    }
    SECTION("segment durations set the time axis") {
        t.segments[0].cycles = 12; // 12 + 4 cycles: segment 1 starts at slice 96
        const auto sl = slices::from_wavetable(t, w);
        CHECK(sl.keyframe[96]);
        CHECK_FALSE(sl.keyframe[64]);
        CHECK(sl.rows[48][0] == Approx(0.5f));
    }
}

TEST_CASE("CYCLE slices start every row at a rising zero crossing", "[slices]") {
    constexpr std::uint32_t kRate = 48000;
    constexpr double kHz = 440.0;
    std::vector<float> pcm(kRate);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        // A fade-in (loudness shows) and a start mid-cycle (alignment has to find the crossing).
        pcm[i] = static_cast<float>((0.1 + 0.9 * t) * std::sin(2.0 * std::numbers::pi * kHz * t + 2.0));
    }
    const auto sample = make_sample("SINE", "", kRate, pcm);
    // The root a little off (as when ROOT is not set exactly): the tracking still keeps the phase.
    for (const double period : {kRate / kHz, kRate / kHz * 1.03}) {
        INFO("period " << period);
        const auto sl = slices::from_sample(*sample, 0, sample->frames, period, slices::SampleView::Cycle);
        for (std::size_t k = 0; k < 128; ++k) {
            INFO("row " << k);
            const float amp = static_cast<float>(0.1 + 0.9 * k / 127.0);
            CHECK(std::fabs(sl.rows[k][0]) < 0.03f);
            CHECK(sl.rows[k][32] == Approx(amp).margin(0.06)); // a quarter period in: the peak
            CHECK(sl.rows[k][96] == Approx(-amp).margin(0.06));
            CHECK_FALSE(sl.keyframe[k]);
        }
    }
}

TEST_CASE("SPAN slices cut the range into 128 x 128 equal bins", "[slices]") {
    std::vector<float> pcm(2 * 128 * 128);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = (i % 2 == 0 ? 1.0f : -1.0f) * static_cast<float>(i / 2) / (128 * 128); // bin value with alternating sign
    }
    pcm[3] = -0.5f; // the value furthest from zero wins, with its sign
    const auto sample = make_sample("RAMP", "", 48000, pcm);
    const auto sl = slices::from_sample(*sample, 0, sample->frames, 100.0, slices::SampleView::Span);
    CHECK(sl.rows[0][1] == Approx(-0.5f / sample->peak()));
    CHECK(sl.rows[5][7] == Approx(static_cast<float>(5 * 128 + 7) / (128 * 128) / sample->peak()).margin(1e-4));
    // A range: the second half only.
    const auto half = slices::from_sample(*sample, sample->frames / 2, sample->frames, 100.0, slices::SampleView::Span);
    CHECK(half.rows[0][0] == Approx(0.5f / sample->peak()).margin(1e-4));
}

TEST_CASE("empty sources give silent slices", "[slices]") {
    const auto t = slices::from_wavetable(WavetableData{}, WavetableParams{});
    REQUIRE(t.rows.size() == 128);
    CHECK(t.rows[5][5] == 0.0f);
    const auto sample = make_sample("ONE", "", 48000, std::vector<float>(1, 0.5f));
    const auto s = slices::from_sample(*sample, 0, 1, 100.0, slices::SampleView::Cycle);
    CHECK(s.rows[0][0] == 0.0f);
}
