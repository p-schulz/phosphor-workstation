#include "dsp/limiter.h"

#include <algorithm>
#include <cmath>

#include "dsp/smoother.h"

namespace phos::dsp {

void PeakLimiter::prepare(float sample_rate) noexcept {
    sample_rate_ = sample_rate;
    set_release_ms(release_ms_);
    reset();
}

void PeakLimiter::set_ceiling_db(float db) noexcept { ceiling_ = db_to_gain(std::min(db, 0.0f)); }

void PeakLimiter::set_release_ms(float ms) noexcept {
    release_ms_ = std::max(ms, 1.0f);
    release_coef_ = std::exp(-1.0f / (release_ms_ * 0.001f * sample_rate_));
}

void PeakLimiter::process(float* left, float* right, std::size_t frames) noexcept {
    float g = gain_;
    float lowest = min_gain_;
    for (std::size_t i = 0; i < frames; ++i) {
        const float peak = std::max(std::fabs(left[i]), std::fabs(right[i]));
        const float target = peak > ceiling_ ? ceiling_ / peak : 1.0f;
        // Attack: jump down at once. Release: glide up towards the target, never past it.
        g = target < g ? target : target + (g - target) * release_coef_;
        left[i] *= g;
        right[i] *= g;
        lowest = std::min(lowest, g);
    }
    gain_ = g;
    min_gain_ = lowest;
}

} // namespace phos::dsp
