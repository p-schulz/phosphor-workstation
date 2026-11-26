#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace phos::dsp {

// In-place iterative radix-2 FFT. `data.size()` must be a power of two. Not realtime-safe in the
// sense of PLAN.md §6.2 only because callers allocate the buffer; the transform itself does not.
void fft(std::span<std::complex<double>> data);

struct ChannelAnalysis {
    double peak = 0.0;      // linear, max |x|
    double peak_db = -200.0;
    double rms_db = -200.0;
    double dc_offset = 0.0; // mean
};

struct Analysis {
    std::size_t frames = 0;
    unsigned sample_rate = 0;
    std::vector<ChannelAnalysis> channels;
    double centroid_hz = 0.0; // spectral centroid of the mono sum, averaged over 4096-frame Hann windows
};

// `interleaved` holds frames * channels samples.
Analysis analyze(std::span<const float> interleaved, unsigned channels, unsigned sample_rate);

} // namespace phos::dsp
