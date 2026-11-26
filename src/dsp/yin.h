#pragma once

#include <span>

namespace phos::dsp {

struct PitchEstimate {
    double hz = 0.0;
    double confidence = 0.0; // 1 - cumulative mean normalised difference at the chosen lag
};

// YIN fundamental-frequency estimator (A. de Cheveigné, H. Kawahara, "YIN, a fundamental frequency
// estimator for speech and music", JASA 111(4), 2002): steps 2-5 (difference function, cumulative
// mean normalisation, absolute threshold 0.15, parabolic interpolation). Searches 30..2000 Hz.
PitchEstimate yin(std::span<const float> mono, double sample_rate);

} // namespace phos::dsp
