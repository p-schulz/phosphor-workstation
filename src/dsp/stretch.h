#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Offline time-stretch / pitch-shift for loop STRETCH mode (PLAN.md §7.5), built on Signalsmith
// Stretch. Runs on a worker thread (allocates); never call it from the audio thread.
namespace phos::dsp {

struct StretchInput {
    const float* left = nullptr;
    const float* right = nullptr; // nullptr = mono
    std::size_t frames = 0;
    std::uint32_t sample_rate = 48000;
};

struct StretchOutput {
    std::vector<float> left;
    std::vector<float> right; // empty when the input was mono
};

// Stretches the input to `out_frames` frames and transposes it by `semitones`. `loop`: the input is
// one cycle of a loop, so the stretcher sees the loop's end before its start and its start after
// its end, and the result loops seamlessly too.
StretchOutput stretch(const StretchInput& in, std::size_t out_frames, float semitones, bool loop);

} // namespace phos::dsp
