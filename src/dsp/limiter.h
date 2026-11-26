#pragma once

#include <cstddef>

namespace phos::dsp {

// Stereo-linked brickwall peak limiter for the master bus. Instant attack (no look-ahead, so no
// added latency) and exponential release. The gain never exceeds ceiling / |peak| for the current
// sample, so the output is guaranteed to stay at or below the ceiling.
class PeakLimiter {
public:
    void prepare(float sample_rate) noexcept;
    void set_ceiling_db(float db) noexcept;
    void set_release_ms(float ms) noexcept;
    void reset() noexcept { gain_ = 1.0f; min_gain_ = 1.0f; }

    // In place on two channel buffers of `frames` samples.
    void process(float* left, float* right, std::size_t frames) noexcept;

    // Lowest gain since the last call (1 = no reduction). Audio thread only.
    float take_min_gain() noexcept {
        const float g = min_gain_;
        min_gain_ = gain_;
        return g;
    }
    float ceiling() const noexcept { return ceiling_; }

private:
    float sample_rate_ = 48000.0f;
    float ceiling_ = 0.891251f; // -1 dBFS
    float release_ms_ = 80.0f;
    float release_coef_ = 0.0f;
    float gain_ = 1.0f;
    float min_gain_ = 1.0f;
};

} // namespace phos::dsp
