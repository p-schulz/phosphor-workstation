#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp/svf.h"
#include "model/instrument.h"

namespace phos::engine {

// One synthesised DRUM hit (PLAN.md §7.3), no samples needed:
//   KICK  sine with an exponential pitch drop + a short noise click
//   TOM   the same, higher and with less drop
//   SNARE 180 Hz tone + high-passed noise
//   HAT   six detuned square waves (metallic ratios) through a high-pass
//   CLAP  band-passed noise in three quick bursts and a tail
//   CHIP  15-bit LFSR noise like a game-console noise channel
// Hits are one-shots: note-off is ignored; choke() fades a hit out within ~15 ms.
class DrumVoice {
public:
    void start(const model::DrumPad& pad, float velocity, float rate, std::uint32_t seed);
    void choke() { choking_ = true; }
    void kill() { active_ = false; }
    bool active() const { return active_; }
    float level() const { return env_ * velocity_; }
    int choke_group() const { return pad_.choke; }

    void render(float* left, float* right, std::size_t frames) noexcept;

private:
    float noise() noexcept;

    model::DrumPad pad_;
    bool active_ = false;
    bool choking_ = false;
    float rate_ = 48000.0f;
    float velocity_ = 1.0f;
    float gain_l_ = 1.0f;
    float gain_r_ = 1.0f;
    double t_ = 0.0;           // seconds since the hit
    float env_ = 1.0f;
    float decay_coef_ = 0.999f;
    float choke_gain_ = 1.0f;
    double phase_ = 0.0;
    double phases_[6] = {};
    std::uint32_t rng_ = 1;
    std::uint16_t lfsr_ = 0x7FFF;
    double lfsr_acc_ = 0.0;
    float lfsr_out_ = 1.0f;
    dsp::Svf filter_;
};

} // namespace phos::engine
