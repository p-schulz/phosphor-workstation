#pragma once

#include <cstddef>

#include "engine/voice_chain.h"
#include "engine/wavetables.h"

namespace phos::engine {

// One WAVETABLE note (PLAN.md §7.3): a phase accumulator reads the current segment's band-limited
// table (linear interpolation); segments advance after their duration in cycles, loop between
// loop start and loop start + length, and crossfade into the next segment when enabled.
class WaveVoice {
public:
    void start(const WaveTables* tables, const model::SamplerParams& p, int note, float velocity, float rate);
    void release() { chain_.release(); }
    void kill() {
        chain_.kill();
        tables_ = nullptr;
    }
    bool active() const { return tables_ && chain_.active(); }
    float level() const { return chain_.level(); }
    const WaveTables* tables() const { return tables_; }

    void render(float* left, float* right, std::size_t frames, const model::SamplerParams& p,
                const model::WavetableParams& w, double pitch) noexcept;

private:
    int next_segment(int s, const model::WavetableParams& w) const;

    const WaveTables* tables_ = nullptr;
    VoiceChain chain_;
    int note_ = 60;
    float rate_ = 48000.0f;
    double phase_ = 0.0;
    int segment_ = 0;
    int cycles_done_ = 0;
};

} // namespace phos::engine
