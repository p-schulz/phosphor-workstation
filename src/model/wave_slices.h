#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "model/instrument.h"
#include "model/sample_data.h"

// Page D's view of a sound: always 128 slices of 128 points, the 128-segment memory of a wavetable
// voice (PLAN.md §7.3). A shorter wavetable is filled in between its segments the way the engine
// plays it; a sample is cut into 128 equal time slices. Pure functions, UI thread.
namespace phos::model::slices {

inline constexpr int kSlices = 128;
inline constexpr int kPoints = 128;

struct Slices {
    std::vector<std::array<float, kPoints>> rows; // kSlices rows, values -1..1, row 0 = start of the sound
    std::vector<bool> keyframe;                   // the row starts a real segment (wavetables; samples: none)
};

// Row k is the sound at time k / 128 of one pass through all segments, timed by each segment's
// cycles. With crossfade the wave between two segments is their linear mix (as WaveVoice plays it;
// the last loop segment fades to the loop start), without crossfade it is the current segment.
Slices from_wavetable(const WavetableData& table, const WavetableParams& params);

enum class SampleView {
    Cycle, // one period (period_frames) per slice, starting at a rising zero crossing
    Span,  // the slice itself, 128 points (each the bin's value furthest from zero): for drums, noise
};

// Slices of the mono mix of [start, end) (clamped). `period_frames` is the length of one cycle at the
// sample's root note (Cycle only). Both views scale by the peak of the range, so loudness shows.
Slices from_sample(const SampleData& sample, std::size_t start, std::size_t end, double period_frames,
                   SampleView view);

} // namespace phos::model::slices
