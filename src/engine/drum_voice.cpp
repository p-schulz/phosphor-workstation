#include "engine/drum_voice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::engine {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
// Metallic oscillator ratios for the hat (the classic six-square-wave hi-hat recipe).
constexpr double kHatHz[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};

} // namespace

float DrumVoice::noise() noexcept {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(static_cast<std::int32_t>(rng_)) / 2147483648.0f;
}

void DrumVoice::start(const model::DrumPad& pad, float velocity, float rate, std::uint32_t seed) {
    pad_ = pad;
    active_ = pad.type != model::DrumType::Off && pad.type != model::DrumType::Sample;
    choking_ = false;
    choke_gain_ = 1.0f;
    rate_ = rate;
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    t_ = 0.0;
    env_ = 1.0f;
    // Amplitude reaches -60 dB after decay_ms.
    decay_coef_ = std::exp(std::log(0.001f) / std::max(1.0f, pad.decay_ms * 0.001f * rate));
    const float level = std::pow(10.0f, pad.level_db / 20.0f) * velocity_;
    const float theta = (std::clamp(pad.pan, -1.0f, 1.0f) + 1.0f) * std::numbers::pi_v<float> / 4.0f;
    gain_l_ = level * std::cos(theta) * std::numbers::sqrt2_v<float>;
    gain_r_ = level * std::sin(theta) * std::numbers::sqrt2_v<float>;
    phase_ = 0.0;
    for (double& p : phases_) {
        p = 0.0;
    }
    rng_ = seed | 1u;
    lfsr_ = 0x7FFF;
    lfsr_acc_ = 0.0;
    filter_.reset();
    const float tune = std::exp2(pad.tune / 12.0f);
    switch (pad.type) {
    case model::DrumType::Snare:
        filter_.set(2500.0f * tune, 0.2f, rate);
        break;
    case model::DrumType::Hat:
        filter_.set((6000.0f + pad.tone * 6000.0f) * tune, 0.3f, rate);
        break;
    case model::DrumType::Clap:
        filter_.set(1200.0f * tune, 0.55f, rate);
        break;
    default:
        break;
    }
}

void DrumVoice::render(float* left, float* right, std::size_t frames) noexcept {
    if (!active_) {
        return;
    }
    const double dt = 1.0 / rate_;
    const double tune = std::exp2(pad_.tune / 12.0);
    for (std::size_t i = 0; i < frames; ++i) {
        float v = 0.0f;
        switch (pad_.type) {
        case model::DrumType::Kick:
        case model::DrumType::Tom: {
            const bool kick = pad_.type == model::DrumType::Kick;
            const double base = (kick ? 50.0 : 110.0) * tune;
            const double drop = kick ? 4.0 : 1.2;
            const double hz = base * (1.0 + drop * std::exp(-t_ / (kick ? 0.03 : 0.05)));
            phase_ += hz * dt;
            phase_ -= std::floor(phase_);
            v = static_cast<float>(std::sin(kTwoPi * phase_));
            if (kick) { // click: a few ms of noise
                v += pad_.tone * 0.6f * noise() * static_cast<float>(std::exp(-t_ / 0.0015));
            }
            break;
        }
        case model::DrumType::Snare: {
            phase_ += 180.0 * tune * dt;
            phase_ -= std::floor(phase_);
            const auto tone_env = static_cast<float>(std::exp(-t_ / 0.04));
            const float body = static_cast<float>(std::sin(kTwoPi * phase_)) * tone_env;
            const float snap = filter_.process(noise(), dsp::Svf::Mode::HighPass);
            v = body * (1.0f - pad_.tone) + snap * (0.3f + pad_.tone);
            break;
        }
        case model::DrumType::Hat: {
            float sum = 0.0f;
            for (int k = 0; k < 6; ++k) {
                phases_[k] += kHatHz[k] * 2.0 * tune * dt;
                phases_[k] -= std::floor(phases_[k]);
                sum += phases_[k] < 0.5 ? 1.0f : -1.0f;
            }
            v = filter_.process(sum / 6.0f, dsp::Svf::Mode::HighPass) * 1.5f;
            break;
        }
        case model::DrumType::Clap: {
            // Three bursts 10 ms apart, each decaying in ~4 ms, then the tail (tone = tail level).
            const double burst = std::fmod(t_, 0.010);
            const float bursts = t_ < 0.030 ? static_cast<float>(std::exp(-burst / 0.004)) : 0.0f;
            const float tail = t_ >= 0.020 ? pad_.tone : 0.0f;
            v = filter_.process(noise(), dsp::Svf::Mode::BandPass) * 2.0f * std::max(bursts, tail);
            break;
        }
        case model::DrumType::Chip: {
            // Game Boy-style noise: 15-bit LFSR, taps 0 and 1 (Pan Docs, "Noise channel").
            lfsr_acc_ += (2000.0 + 30000.0 * pad_.tone) * tune * dt;
            while (lfsr_acc_ >= 1.0) { // bounded: rate < 4 x 32 kHz
                lfsr_acc_ -= 1.0;
                const std::uint16_t bit = static_cast<std::uint16_t>((lfsr_ ^ (lfsr_ >> 1)) & 1u);
                lfsr_ = static_cast<std::uint16_t>((lfsr_ >> 1) | (bit << 14));
                lfsr_out_ = (lfsr_ & 1u) ? -0.8f : 0.8f;
            }
            v = lfsr_out_;
            break;
        }
        case model::DrumType::Off:
        case model::DrumType::Sample:
            active_ = false;
            return;
        }
        if (choking_) {
            choke_gain_ *= 0.99f; // 2 ms time constant: silent within ~15 ms
        }
        const float a = v * env_ * choke_gain_;
        left[i] += a * gain_l_;
        right[i] += a * gain_r_;
        env_ *= decay_coef_;
        t_ += dt;
        if (env_ < 1e-4f || choke_gain_ < 1e-3f) {
            active_ = false;
            return;
        }
    }
}

} // namespace phos::engine
