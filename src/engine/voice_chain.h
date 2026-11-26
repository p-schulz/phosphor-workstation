#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "dsp/adsr.h"
#include "dsp/svf.h"
#include "model/instrument.h"

namespace phos::engine {

// Amp ADSR + filter ADSR + 12 dB SVF + velocity, level and pan, shared by the WAVETABLE and SYNTH
// voices (the parameters live in model::SamplerParams, common to every instrument kind).
class VoiceChain {
public:
    void start(const model::SamplerParams& p, int note, float velocity, float rate) {
        note_ = note;
        velocity_ = std::clamp(velocity, 0.0f, 1.0f);
        rate_ = rate;
        amp_.kill();
        fenv_.kill();
        set(p);
        amp_.gate_on();
        fenv_.gate_on();
        svf_l_.reset();
        svf_r_.reset();
    }
    void retrigger_note(int note) { note_ = note; } // legato: new key tracking, no envelope restart
    void release() {
        amp_.gate_off();
        fenv_.gate_off();
    }
    void kill() { amp_.kill(); }
    bool active() const { return amp_.active(); }
    bool releasing() const { return amp_.stage() == dsp::Adsr::Stage::Release; }
    float level() const { return amp_.level() * velocity_; }

    // Once per chunk: envelope times, gain, pan, filter coefficients (cutoff_octaves adds an LFO).
    void chunk(const model::SamplerParams& p, float cutoff_octaves = 0.0f) {
        set(p);
        gain_ = (1.0f - p.vel_to_amp + p.vel_to_amp * velocity_) * std::pow(10.0f, p.volume_db / 20.0f);
        const float theta = (std::clamp(p.pan, -1.0f, 1.0f) + 1.0f) * std::numbers::pi_v<float> / 4.0f;
        pl_ = std::cos(theta) * std::numbers::sqrt2_v<float>;
        pr_ = std::sin(theta) * std::numbers::sqrt2_v<float>;
        filtered_ = p.filter != model::FilterType::Off;
        mode_ = p.filter == model::FilterType::HighPass  ? dsp::Svf::Mode::HighPass
                : p.filter == model::FilterType::BandPass ? dsp::Svf::Mode::BandPass
                                                           : dsp::Svf::Mode::LowPass;
        if (filtered_) {
            const float oct = p.env_amount * fenv_.level() + p.key_track * static_cast<float>(note_ - 60) / 12.0f +
                              p.vel_to_cutoff * velocity_ + cutoff_octaves;
            svf_l_.set(p.cutoff_hz * std::exp2(oct), p.resonance, rate_);
            svf_r_.set(p.cutoff_hz * std::exp2(oct), p.resonance, rate_);
        }
    }

    // Per sample: filter, envelope, gain, pan; adds into out_l / out_r. `amp_mod` multiplies (tremolo).
    void process(float l, float r, float amp_mod, float& out_l, float& out_r) {
        const float env = amp_.next() * gain_ * amp_mod;
        fenv_.next();
        if (filtered_) {
            l = svf_l_.process(l, mode_);
            r = svf_r_.process(r, mode_);
        }
        out_l += l * env * pl_;
        out_r += r * env * pr_;
    }

private:
    void set(const model::SamplerParams& p) {
        amp_.set(p.amp.attack_ms, p.amp.decay_ms, p.amp.sustain, p.amp.release_ms, rate_);
        fenv_.set(p.filter_env.attack_ms, p.filter_env.decay_ms, p.filter_env.sustain, p.filter_env.release_ms, rate_);
    }

    dsp::Adsr amp_;
    dsp::Adsr fenv_;
    dsp::Svf svf_l_;
    dsp::Svf svf_r_;
    dsp::Svf::Mode mode_ = dsp::Svf::Mode::LowPass;
    bool filtered_ = false;
    int note_ = 60;
    float velocity_ = 1.0f;
    float rate_ = 48000.0f;
    float gain_ = 1.0f;
    float pl_ = 1.0f;
    float pr_ = 1.0f;
};

} // namespace phos::engine
