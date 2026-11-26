#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::dsp {

// State-variable filter, trapezoidal (TPT) integration after A. Simper (Cytomic), "Linear
// Trapezoidal Integrated SVF" (2013), 12 dB/oct. Low-, band- and high-pass from one structure;
// stable under fast cutoff modulation. Coefficients are set per block, state per sample.
class Svf {
public:
    enum class Mode { LowPass, HighPass, BandPass };

    // resonance 0..1 maps to damping k = 2 .. 0.05 (Q 0.5 .. 20).
    void set(float cutoff_hz, float resonance, float sample_rate) noexcept {
        const float fc = std::clamp(cutoff_hz, 10.0f, sample_rate * 0.45f);
        const float g = std::tan(std::numbers::pi_v<float> * fc / sample_rate);
        k_ = 2.0f - 1.95f * std::clamp(resonance, 0.0f, 1.0f);
        a1_ = 1.0f / (1.0f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }
    void reset() noexcept { ic1_ = ic2_ = 0.0f; }

    float process(float v0, Mode mode) noexcept {
        const float v3 = v0 - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        switch (mode) {
        case Mode::HighPass:
            return v0 - k_ * v1 - v2;
        case Mode::BandPass:
            return v1;
        case Mode::LowPass:
            break;
        }
        return v2;
    }

private:
    float k_ = 2.0f;
    float a1_ = 1.0f;
    float a2_ = 0.0f;
    float a3_ = 0.0f;
    float ic1_ = 0.0f;
    float ic2_ = 0.0f;
};

} // namespace phos::dsp
