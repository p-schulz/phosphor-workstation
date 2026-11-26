#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/voice_chain.h"

namespace phos::engine {

// One SYNTH note (PLAN.md §7.3): two PolyBLEP oscillators (saw, pulse with PWM, triangle, sine,
// noise) with unison 1-4, a sub oscillator, noise, an LFO (sine / triangle / square / S&H, free or
// tempo-synced) to pitch, cutoff, pulse width and amp, and glide. Then the shared VoiceChain.
class SynthVoice {
public:
    // `from_note` < 0: no glide.
    void start(const model::SamplerParams& p, const model::SynthParams& s, int note, int from_note, float velocity,
               float rate, std::uint32_t seed);
    // Mono legato: glide to `note` without restarting the envelopes.
    void legato(int note) {
        target_ = note;
        chain_.retrigger_note(note);
    }
    void release() { chain_.release(); }
    void kill() { chain_.kill(); }
    bool active() const { return chain_.active(); }
    bool releasing() const { return chain_.releasing(); }
    float level() const { return chain_.level(); }
    int note() const { return static_cast<int>(target_); }

    void render(float* left, float* right, std::size_t frames, const model::SamplerParams& p,
                const model::SynthParams& s, double pitch, double bpm) noexcept;

    // PolyBLEP correction (Välimäki et al.): t = phase in [0,1), dt = phase increment.
    static float polyblep(double t, double dt) noexcept {
        if (t < dt) {
            t /= dt;
            return static_cast<float>(t + t - t * t - 1.0);
        }
        if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return static_cast<float>(t * t + t + t + 1.0);
        }
        return 0.0f;
    }
    static float osc(model::OscWave w, double t, double dt, float pw, std::uint32_t& noise) noexcept;

private:
    float lfo_value(const model::SynthParams& s) noexcept;

    VoiceChain chain_;
    float rate_ = 48000.0f;
    double current_ = 60.0; // sounding pitch in semitones (glides towards target_)
    double target_ = 60.0;
    std::array<double, 4> ph1_{};
    std::array<double, 4> ph2_{};
    double sub_ = 0.0;
    double lfo_ = 0.0;
    float sh_ = 0.0f;
    std::uint32_t noise_ = 1;
};

} // namespace phos::engine
