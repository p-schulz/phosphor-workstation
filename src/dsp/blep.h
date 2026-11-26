#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace phos::dsp {

// Band-limited step synthesis (PLAN.md §7.4): a waveform made of amplitude steps (chip channels)
// is built by adding, for every step, a band-limited impulse at the step's exact fractional time
// and integrating. The impulse is a Blackman-windowed sinc, 16 taps, tabulated at 32 sub-sample
// phases and normalised so every phase sums to 1 (a step adds exactly its height). Output is
// delayed by 8 samples (linear phase). In RAW mode steps land on whole samples (naive aliasing).
// Own implementation from the textbook method; no code from blip_buf (LGPL).
class BlepBuffer {
public:
    static constexpr int kTaps = 16;
    static constexpr int kPhases = 32;

    BlepBuffer() { table(); }

    void reset(float level = 0.0f) {
        ring_.fill(0.0f);
        pos_ = 0;
        sum_ = level;
    }

    // A step of `delta` at `frac` (0..1) into the sample about to be read.
    void add_step(float frac, float delta, bool raw = false) {
        if (raw) {
            ring_[(pos_ + kTaps / 2) & kMask] += delta;
            return;
        }
        const int phase = std::clamp(static_cast<int>(frac * kPhases), 0, kPhases - 1);
        const auto& k = table()[static_cast<std::size_t>(phase)];
        for (int t = 0; t < kTaps; ++t) {
            ring_[(pos_ + static_cast<std::size_t>(t)) & kMask] += delta * k[static_cast<std::size_t>(t)];
        }
    }

    // Next output sample.
    float read() {
        sum_ += ring_[pos_];
        ring_[pos_] = 0.0f;
        pos_ = (pos_ + 1) & kMask;
        return sum_;
    }

private:
    static constexpr std::size_t kSize = 32;
    static constexpr std::size_t kMask = kSize - 1;
    using Table = std::array<std::array<float, kTaps>, kPhases>;

    static const Table& table() {
        static const Table t = [] {
            Table tab{};
            constexpr double cutoff = 0.90; // fraction of Nyquist
            for (int p = 0; p < kPhases; ++p) {
                const double frac = static_cast<double>(p) / kPhases;
                double sum = 0.0;
                for (int i = 0; i < kTaps; ++i) {
                    // Distance from the step time to tap i's sample time (centre at tap 8).
                    const double x = static_cast<double>(i - kTaps / 2) + (1.0 - frac);
                    const double s = x == 0.0 ? cutoff : std::sin(std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
                    const double n = (x + kTaps / 2.0) / kTaps; // 0..1 across the window
                    const double w = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * n) + 0.08 * std::cos(4.0 * std::numbers::pi * n);
                    tab[static_cast<std::size_t>(p)][static_cast<std::size_t>(i)] = static_cast<float>(s * std::max(0.0, w));
                    sum += s * std::max(0.0, w);
                }
                for (auto& v : tab[static_cast<std::size_t>(p)]) {
                    v = static_cast<float>(v / sum);
                }
            }
            return tab;
        }();
        return t;
    }

    std::array<float, kSize> ring_{};
    std::size_t pos_ = 0;
    float sum_ = 0.0f;
};

} // namespace phos::dsp
