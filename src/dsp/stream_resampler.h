#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace phos::dsp {

// Streaming stereo resampler for the SNES bus (32 kHz -> device rate): 16-tap Kaiser-windowed
// sinc, 64 tabulated phases with linear interpolation between them; latency 8 input samples.
// Realtime-safe (fixed ring, no allocation). Chosen over r8brain's streaming mode for its
// latency (live playing); the SNES signal is already band-limited by the Gaussian interpolation.
class StreamResampler {
public:
    static constexpr int kTaps = 16;
    static constexpr int kPhases = 64;
    static constexpr std::size_t kRing = 4096; // input history (power of two)

    void prepare(double in_rate, double out_rate) {
        step_ = in_rate / out_rate;
        const double cutoff = std::min(1.0, out_rate / in_rate) * 0.92;
        constexpr double beta = 7.0;
        for (int p = 0; p <= kPhases; ++p) {
            double sum = 0.0;
            for (int i = 0; i < kTaps; ++i) {
                const double x = static_cast<double>(i - kTaps / 2 + 1) - static_cast<double>(p) / kPhases;
                const double s = x == 0.0 ? cutoff : std::sin(std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
                const double r = x / (kTaps / 2.0);
                const double w = r * r < 1.0 ? bessel0(beta * std::sqrt(1.0 - r * r)) / bessel0(beta) : 0.0;
                table_[static_cast<std::size_t>(p)][static_cast<std::size_t>(i)] = s * w;
                sum += s * w;
            }
            for (auto& v : table_[static_cast<std::size_t>(p)]) {
                v /= sum;
            }
        }
        reset();
    }

    void reset() {
        l_.fill(0.0f);
        r_.fill(0.0f);
        written_ = kTaps; // a zero history
        next_ = kTaps / 2.0;
    }

    // Input samples needed (beyond those already pushed) to produce `out` more outputs.
    std::size_t needed(std::size_t out) const {
        if (out == 0) {
            return 0;
        }
        const double last = next_ + step_ * static_cast<double>(out - 1);
        const auto need = static_cast<std::size_t>(std::floor(last)) + kTaps / 2 + 1;
        return need > written_ ? need - written_ : 0;
    }

    void push(const float* l, const float* r, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            l_[(written_ + i) & (kRing - 1)] = l[i];
            r_[(written_ + i) & (kRing - 1)] = r[i];
        }
        written_ += n;
    }

    // Adds `n` output frames into out_l / out_r (push() the needed input first).
    void pull_add(float* out_l, float* out_r, std::size_t n) {
        for (std::size_t o = 0; o < n; ++o) {
            const double fl = std::floor(next_);
            const double frac = (next_ - fl) * kPhases;
            const auto ph = static_cast<std::size_t>(frac);
            const double t = frac - static_cast<double>(ph);
            const auto base = static_cast<std::size_t>(fl) - kTaps / 2 + 1;
            double sl = 0.0;
            double sr = 0.0;
            for (std::size_t i = 0; i < kTaps; ++i) {
                const double c = table_[ph][i] + (table_[ph + 1][i] - table_[ph][i]) * t;
                sl += c * l_[(base + i) & (kRing - 1)];
                sr += c * r_[(base + i) & (kRing - 1)];
            }
            out_l[o] += static_cast<float>(sl);
            out_r[o] += static_cast<float>(sr);
            next_ += step_;
        }
    }

private:
    static double bessel0(double x) {
        double sum = 1.0;
        double term = 1.0;
        for (int k = 1; k < 30; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    std::array<std::array<double, kTaps>, kPhases + 1> table_{};
    std::array<float, kRing> l_{};
    std::array<float, kRing> r_{};
    std::size_t written_ = kTaps;
    double next_ = kTaps / 2.0;
    double step_ = 32000.0 / 48000.0;
};

} // namespace phos::dsp
