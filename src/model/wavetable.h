#pragma once

#include <cstddef>

#include "model/instrument.h"

// WAVETABLE editing (PLAN.md §7.3, Pages 4, 6 and 0). Every function returns new data.
namespace phos::model::wavetable {

inline constexpr int kPoints = 128;
inline constexpr int kHarmonics = 32;

// COMPUTE: wave[i] = sum over h of harmonics[h-1] * sin(2 pi h i / 128), a direct sum of sines
// per segment (no FFT). With `normalize` the whole table is scaled to a peak of 1.
WavetableData compute(const WavetableData& in, bool normalize);

// Resizes to `count` segments (1..128); new segments copy the last one.
WavetableData resize(const WavetableData& in, int count);

// Page 6 drawing tools on one segment.
WavetableData set_points(const WavetableData& in, int segment, int from, int to, float from_value, float to_value); // LINE
WavetableData smooth(const WavetableData& in, int segment);   // 3-point moving average, circular
WavetableData copy_segment(const WavetableData& in, int from, int to);
// Morphs the segments strictly between `a` and `b` linearly from a to b (wave and harmonics).
WavetableData interpolate(const WavetableData& in, int a, int b);

// Page 4 / Page 0: sets one harmonic amplitude of one segment (0..1).
WavetableData set_harmonic(const WavetableData& in, int segment, int harmonic, float value);

// A bright start: 16 segments whose upper harmonics fade out over time (a plucked saw).
WavetableData default_table();

} // namespace phos::model::wavetable
