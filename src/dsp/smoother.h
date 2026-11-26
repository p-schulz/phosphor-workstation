#pragma once

#include <cmath>

namespace phos::dsp {

// One-pole parameter smoother (PLAN.md §6.3: continuous parameter moves, ~5 ms). Moves 63 % of the
// remaining distance per time constant; snaps to the target once within 1e-6 so it can go idle.
class OnePoleSmoother {
public:
    void prepare(float sample_rate, float time_ms) noexcept {
        const float tau_frames = time_ms * 0.001f * sample_rate;
        coef_ = tau_frames > 0.0f ? std::exp(-1.0f / tau_frames) : 0.0f;
    }
    void reset(float value) noexcept {
        value_ = value;
        target_ = value;
    }
    void set_target(float target) noexcept { target_ = target; }
    float target() const noexcept { return target_; }
    float value() const noexcept { return value_; }
    bool settled() const noexcept { return value_ == target_; }

    float next() noexcept {
        if (value_ != target_) {
            const float next = target_ + (value_ - target_) * coef_;
            // Snap when close, or when float rounding stops the approach (it can stall a few
            // ulps short of the target).
            value_ = (next == value_ || std::fabs(next - target_) < 1e-6f) ? target_ : next;
        }
        return value_;
    }

private:
    float coef_ = 0.0f;
    float value_ = 0.0f;
    float target_ = 0.0f;
};

inline float db_to_gain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
inline float gain_to_db(float gain) noexcept { return gain > 1e-10f ? 20.0f * std::log10(gain) : -200.0f; }

} // namespace phos::dsp
