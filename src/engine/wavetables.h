#pragma once

#include <memory>
#include <vector>

#include "model/instrument.h"

namespace phos::engine {

// Band-limited copies of every wavetable segment (PLAN.md §7.3 anti-aliasing): mip level l keeps
// harmonics 1 .. 64 >> l and zeroes the rest, so a note can use the richest level whose top
// harmonic stays below Nyquist. Built on the UI thread when the wavetable changes; immutable.
struct WaveTables {
    static constexpr int kPoints = 128;
    static constexpr int kLevels = 7; // 64, 32, 16, 8, 4, 2, 1 harmonics
    static constexpr int kStride = kPoints + 1; // one guard sample (= first) for interpolation

    int segments = 0;
    std::vector<int> cycles;  // per segment
    std::vector<float> data;  // [segment][level][kStride]

    const float* table(int segment, int level) const {
        return data.data() + (static_cast<std::size_t>(segment) * kLevels + static_cast<std::size_t>(level)) * kStride;
    }
    // Richest level whose highest harmonic is below `nyquist_hz` at `freq_hz`.
    static int level_for(double freq_hz, double nyquist_hz) {
        for (int l = 0; l < kLevels - 1; ++l) {
            if ((64 >> l) * freq_hz < nyquist_hz) {
                return l;
            }
        }
        return kLevels - 1;
    }
};

// `previous` / `previous_tables`: the data the old tables were built from; segments whose waveform
// is unchanged are copied instead of transformed again (drawing touches one segment per frame).
std::shared_ptr<const WaveTables> build_tables(const model::WavetableData& data,
                                               const model::WavetableData* previous = nullptr,
                                               const WaveTables* previous_tables = nullptr);

} // namespace phos::engine
