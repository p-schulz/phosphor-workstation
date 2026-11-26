#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace phos::model {

// Waveform overview for drawing (PLAN.md §7.2): min / max / RMS of the mono mix per bin.
struct Overview {
    static constexpr std::size_t kBins = 1024;
    std::vector<float> min;
    std::vector<float> max;
    std::vector<float> rms;
};

// Immutable PCM at the engine rate (PLAN.md §6.3: SampleData is published by pointer, never
// changed in place). Edits produce a new SampleData; the old one stays alive while the undo stack
// or a playing voice still uses it.
struct SampleData {
    std::string name;        // display name (file stem)
    std::string source_path; // original file
    std::uint32_t sample_rate = 48000;
    int channels = 1;        // 1 or 2
    std::size_t frames = 0;
    std::vector<float> left;  // planar
    std::vector<float> right; // empty when mono
    Overview overview;

    const float* data(int channel) const { return channel == 1 && channels == 2 ? right.data() : left.data(); }
    double seconds() const { return sample_rate ? static_cast<double>(frames) / sample_rate : 0.0; }
    std::size_t bytes() const { return (left.size() + right.size()) * sizeof(float); }
    float peak() const;
};

using SampleRef = std::shared_ptr<const SampleData>;

// Builds a SampleData and its overview. `right` empty = mono.
SampleRef make_sample(std::string name, std::string source_path, std::uint32_t sample_rate, std::vector<float> left,
                      std::vector<float> right = {});

// Destructive edits (PLAN.md §7.2 Page 8). Each returns a new SampleData; ranges are [start, end)
// in frames and are clamped to the sample.
namespace edit {
SampleRef trim(const SampleData& s, std::size_t start, std::size_t end);
SampleRef fade_in(const SampleData& s, std::size_t start, std::size_t end);
SampleRef fade_out(const SampleData& s, std::size_t start, std::size_t end);
SampleRef normalize(const SampleData& s, float peak_db = -0.3f);
SampleRef reverse(const SampleData& s, std::size_t start, std::size_t end);
// Equal-power crossfade of the `length` frames before loop_end with the frames before loop_start,
// so the jump from loop_end back to loop_start is seamless. Needs loop_start >= length.
SampleRef crossfade_loop(const SampleData& s, std::size_t loop_start, std::size_t loop_end, std::size_t length);
} // namespace edit

} // namespace phos::model
