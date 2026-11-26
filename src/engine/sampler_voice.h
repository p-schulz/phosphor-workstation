#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp/adsr.h"
#include "dsp/svf.h"
#include "model/instrument.h"
#include "model/sample_data.h"

namespace phos::engine {

// One playing SAMPLER note (PLAN.md §7.3): interpolated playback (DROP / LINEAR / HERMITE),
// forward or ping-pong loop, amp ADSR, filter ADSR into a 12 dB SVF with key tracking and
// velocity. Parameters are read on every render call, so edits apply to held notes.
// Realtime-safe; the SampleData is owned elsewhere and outlives the voice (engine snapshot rules).
class SamplerVoice {
public:
    void start(const model::SampleData* sample, int slot, const model::SamplerParams& p, int note, float velocity,
               float engine_rate) noexcept;
    void release() noexcept;
    void kill() noexcept;
    bool active() const noexcept { return sample_ != nullptr && amp_.active(); }
    bool releasing() const noexcept { return amp_.stage() == dsp::Adsr::Stage::Release; }
    float level() const noexcept { return amp_.level() * velocity_; }
    const model::SampleData* sample() const noexcept { return sample_; }
    int slot() const noexcept { return slot_; }
    int note() const noexcept { return note_; }

    // Adds into left/right. `pitch` multiplies the playback rate (bend, vibrato).
    void render(float* left, float* right, std::size_t frames, const model::SamplerParams& p, double pitch) noexcept;

private:
    float fetch(const float* data, long index) const noexcept;
    float interpolate(const float* data, double pos, model::Interp interp) const noexcept;
    void update_loop(const model::SamplerParams& p) noexcept;

    const model::SampleData* sample_ = nullptr;
    int slot_ = 0;
    int note_ = 60;
    float velocity_ = 1.0f; // 0..1 after the velocity curve
    float engine_rate_ = 48000.0f;
    double pos_ = 0.0;
    int dir_ = 1;
    bool looping_ = false;
    bool ping_pong_ = false;
    long loop_start_ = 0;
    long loop_end_ = 0;
    dsp::Adsr amp_;
    dsp::Adsr fenv_;
    dsp::Svf svf_l_;
    dsp::Svf svf_r_;
};

} // namespace phos::engine
