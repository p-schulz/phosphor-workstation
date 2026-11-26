#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "io/wav.h"

using namespace phos;
using Catch::Approx;

namespace {

io::AudioBuffer ramp() {
    io::AudioBuffer b;
    b.sample_rate = 44100;
    b.channels = 2;
    for (int i = 0; i < 200; ++i) {
        b.samples.push_back(static_cast<float>(i) / 100.0f - 1.0f); // -1 .. 0.99
        b.samples.push_back(0.25f);
    }
    return b;
}

} // namespace

TEST_CASE("wav float32 round trip is exact", "[wav]") {
    const auto b = ramp();
    std::string error;
    const auto back = io::parse_wav(io::encode_wav(b, io::WavFormat::Float32), &error);
    REQUIRE(back);
    CHECK(back->sample_rate == 44100);
    CHECK(back->channels == 2);
    CHECK(back->samples == b.samples);
}

TEST_CASE("wav 16 and 24 bit round trips stay within one step", "[wav]") {
    const auto b = ramp();
    for (const auto [fmt, step] : {std::pair{io::WavFormat::Pcm16, 1.0 / 32767.0}, std::pair{io::WavFormat::Pcm24, 1.0 / 8388607.0}}) {
        const auto back = io::parse_wav(io::encode_wav(b, fmt), nullptr);
        REQUIRE(back);
        REQUIRE(back->samples.size() == b.samples.size());
        for (std::size_t i = 0; i < b.samples.size(); ++i) {
            CHECK(back->samples[i] == Approx(b.samples[i]).margin(step * 1.5));
        }
    }
}

TEST_CASE("wav reader skips unknown chunks and rejects garbage", "[wav]") {
    auto bytes = io::encode_wav(ramp(), io::WavFormat::Pcm16);
    // Insert a LIST chunk with an odd size (padded) between fmt and data.
    const std::vector<unsigned char> list = {'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0};
    bytes.insert(bytes.begin() + 36, list.begin(), list.end());
    const auto back = io::parse_wav(bytes, nullptr);
    REQUIRE(back);
    CHECK(back->frames() == 200);

    std::string error;
    CHECK_FALSE(io::parse_wav({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '}, &error));
    CHECK_FALSE(error.empty());
}
