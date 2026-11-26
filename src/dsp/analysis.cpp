#include "dsp/analysis.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::dsp {

void fft(std::span<std::complex<double>> data) {
    const std::size_t n = data.size();
    if (n < 2) {
        return;
    }
    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = data[i + k];
                const auto v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

namespace {

double to_db(double v) { return v > 1e-10 ? 20.0 * std::log10(v) : -200.0; }

} // namespace

Analysis analyze(std::span<const float> interleaved, unsigned channels, unsigned sample_rate) {
    Analysis a;
    a.sample_rate = sample_rate;
    if (channels == 0) {
        return a;
    }
    a.frames = interleaved.size() / channels;
    a.channels.resize(channels);
    for (unsigned c = 0; c < channels; ++c) {
        double peak = 0.0;
        double sum = 0.0;
        double sum_sq = 0.0;
        for (std::size_t f = 0; f < a.frames; ++f) {
            const double x = interleaved[f * channels + c];
            peak = std::max(peak, std::fabs(x));
            sum += x;
            sum_sq += x * x;
        }
        auto& ch = a.channels[c];
        ch.peak = peak;
        ch.peak_db = to_db(peak);
        if (a.frames > 0) {
            ch.dc_offset = sum / static_cast<double>(a.frames);
            ch.rms_db = to_db(std::sqrt(sum_sq / static_cast<double>(a.frames)));
        }
    }

    // Spectral centroid: magnitude-weighted mean frequency, accumulated over all windows.
    constexpr std::size_t kWindow = 4096;
    std::vector<std::complex<double>> buf(kWindow);
    double weighted = 0.0;
    double total = 0.0;
    const std::size_t hop = kWindow / 2;
    for (std::size_t start = 0; start + kWindow <= std::max(a.frames, kWindow); start += hop) {
        for (std::size_t i = 0; i < kWindow; ++i) {
            double mono = 0.0;
            if (start + i < a.frames) {
                for (unsigned c = 0; c < channels; ++c) {
                    mono += interleaved[(start + i) * channels + c];
                }
            }
            const double hann = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / kWindow);
            buf[i] = {mono / channels * hann, 0.0};
        }
        fft(buf);
        for (std::size_t k = 1; k < kWindow / 2; ++k) {
            const double mag = std::abs(buf[k]);
            weighted += mag * static_cast<double>(k) * sample_rate / kWindow;
            total += mag;
        }
        if (a.frames < kWindow) {
            break;
        }
    }
    a.centroid_hz = total > 0.0 ? weighted / total : 0.0;
    return a;
}

} // namespace phos::dsp
