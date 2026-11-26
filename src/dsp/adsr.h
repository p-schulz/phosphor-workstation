#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace phos::dsp {

// ADSR envelope: linear attack, exponential decay towards the sustain level and exponential
// release, each reaching -80 dB of its distance in the given time. Realtime-safe, no allocation.
class Adsr {
public:
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Sustain, Release };

    void set(float attack_ms, float decay_ms, float sustain, float release_ms, float sample_rate) noexcept {
        attack_step_ = 1.0f / std::max(1.0f, attack_ms * 0.001f * sample_rate);
        decay_coef_ = coef(decay_ms, sample_rate);
        release_coef_ = coef(release_ms, sample_rate);
        sustain_ = std::clamp(sustain, 0.0f, 1.0f);
    }
    void gate_on() noexcept { stage_ = Stage::Attack; }
    void gate_off() noexcept {
        if (stage_ != Stage::Idle) {
            stage_ = Stage::Release;
        }
    }
    void kill() noexcept {
        stage_ = Stage::Idle;
        level_ = 0.0f;
    }
    Stage stage() const noexcept { return stage_; }
    float level() const noexcept { return level_; }
    bool active() const noexcept { return stage_ != Stage::Idle; }

    float next() noexcept {
        switch (stage_) {
        case Stage::Attack:
            level_ += attack_step_;
            if (level_ >= 1.0f) {
                level_ = 1.0f;
                stage_ = Stage::Decay;
            }
            break;
        case Stage::Decay:
            level_ = sustain_ + (level_ - sustain_) * decay_coef_;
            if (level_ - sustain_ < 1e-4f) {
                level_ = sustain_;
                stage_ = Stage::Sustain;
            }
            break;
        case Stage::Sustain:
            level_ = sustain_;
            break;
        case Stage::Release:
            level_ *= release_coef_;
            if (level_ < 1e-4f) {
                kill();
            }
            break;
        case Stage::Idle:
            break;
        }
        return level_;
    }

private:
    static float coef(float ms, float sample_rate) noexcept {
        return std::exp(std::log(1e-4f) / std::max(1.0f, ms * 0.001f * sample_rate));
    }

    Stage stage_ = Stage::Idle;
    float level_ = 0.0f;
    float attack_step_ = 1.0f;
    float decay_coef_ = 0.0f;
    float release_coef_ = 0.0f;
    float sustain_ = 1.0f;
};

} // namespace phos::dsp
