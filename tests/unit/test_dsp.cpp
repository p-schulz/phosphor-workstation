#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "dsp/analysis.h"
#include "dsp/limiter.h"
#include "dsp/smoother.h"

using namespace phos;
using Catch::Approx;

namespace {

std::vector<float> sine(double hz, double amp, unsigned rate, std::size_t frames, unsigned channels = 1) {
    std::vector<float> out(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto v = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
        for (unsigned c = 0; c < channels; ++c) {
            out[i * channels + c] = v;
        }
    }
    return out;
}

} // namespace

TEST_CASE("one-pole smoother reaches 63 percent after one time constant", "[dsp]") {
    dsp::OnePoleSmoother s;
    s.prepare(48000.0f, 5.0f); // tau = 240 frames
    s.reset(0.0f);
    s.set_target(1.0f);
    float v = 0.0f;
    for (int i = 0; i < 240; ++i) {
        v = s.next();
    }
    CHECK(v == Approx(1.0 - std::exp(-1.0)).margin(0.002));
    for (int i = 0; i < 48000; ++i) {
        v = s.next();
    }
    CHECK(s.settled());
    CHECK(v == 1.0f);
}

TEST_CASE("decibel conversions", "[dsp]") {
    CHECK(dsp::db_to_gain(0.0f) == Approx(1.0));
    CHECK(dsp::db_to_gain(-6.0206f) == Approx(0.5).margin(1e-4));
    CHECK(dsp::gain_to_db(0.1f) == Approx(-20.0).margin(1e-4));
}

TEST_CASE("peak limiter never exceeds the ceiling and leaves quiet signals alone", "[dsp]") {
    dsp::PeakLimiter lim;
    lim.prepare(48000.0f);
    lim.set_ceiling_db(-1.0f);
    const float ceiling = dsp::db_to_gain(-1.0f);

    auto loud_l = sine(100.0, 4.0, 48000, 48000);
    auto loud_r = sine(170.0, 3.0, 48000, 48000);
    lim.process(loud_l.data(), loud_r.data(), loud_l.size());
    float peak = 0.0f;
    for (std::size_t i = 0; i < loud_l.size(); ++i) {
        peak = std::max({peak, std::fabs(loud_l[i]), std::fabs(loud_r[i])});
    }
    CHECK(peak <= ceiling + 1e-6f);
    CHECK(peak > ceiling * 0.99f); // actually limiting, not just attenuating
    CHECK(lim.take_min_gain() < 0.3f);

    lim.reset();
    auto quiet_l = sine(440.0, 0.5, 48000, 4800);
    const auto original = quiet_l;
    auto quiet_r = quiet_l;
    lim.process(quiet_l.data(), quiet_r.data(), quiet_l.size());
    CHECK(quiet_l == original);
}

TEST_CASE("fft of a cosine puts the energy in its bin", "[dsp]") {
    constexpr std::size_t n = 64;
    std::vector<std::complex<double>> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::cos(2.0 * std::numbers::pi * 5.0 * static_cast<double>(i) / n);
    }
    dsp::fft(x);
    CHECK(std::abs(x[5]) == Approx(n / 2.0));
    CHECK(std::abs(x[n - 5]) == Approx(n / 2.0));
    CHECK(std::abs(x[6]) == Approx(0.0).margin(1e-9));
}

TEST_CASE("analysis reports peak, rms, dc and centroid of a sine", "[dsp]") {
    auto s = sine(1000.0, 0.5, 48000, 48000, 2);
    for (std::size_t i = 1; i < s.size(); i += 2) {
        s[i] += 0.1f; // DC on the right channel
    }
    const auto a = dsp::analyze(s, 2, 48000);
    REQUIRE(a.channels.size() == 2);
    CHECK(a.frames == 48000);
    CHECK(a.channels[0].peak_db == Approx(20.0 * std::log10(0.5)).margin(0.01));
    CHECK(a.channels[0].rms_db == Approx(20.0 * std::log10(0.5 / std::sqrt(2.0))).margin(0.01));
    CHECK(a.channels[0].dc_offset == Approx(0.0).margin(1e-4));
    CHECK(a.channels[1].dc_offset == Approx(0.1).margin(1e-4));

    const auto clean = dsp::analyze(sine(1000.0, 0.5, 48000, 48000), 1, 48000);
    CHECK(clean.centroid_hz == Approx(1000.0).margin(10.0));
    const auto high = dsp::analyze(sine(5000.0, 0.5, 48000, 48000), 1, 48000);
    CHECK(high.centroid_hz == Approx(5000.0).margin(10.0));
}
