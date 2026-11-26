#include "engine/synth_voice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::engine {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

float white(std::uint32_t& s) noexcept {
    s ^= s << 13; // xorshift32
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(static_cast<std::int32_t>(s)) / 2147483648.0f;
}

double wrap(double p) { return p - std::floor(p); }

} // namespace

float SynthVoice::osc(model::OscWave w, double t, double dt, float pw, std::uint32_t& noise) noexcept {
    switch (w) {
    case model::OscWave::Saw:
        return static_cast<float>(2.0 * t - 1.0) - polyblep(t, dt);
    case model::OscWave::Pulse: {
        float v = t < pw ? 1.0f : -1.0f;
        v += polyblep(t, dt);
        v -= polyblep(wrap(t + 1.0 - pw), dt);
        return v;
    }
    case model::OscWave::Triangle:
        return static_cast<float>(1.0 - 4.0 * std::fabs(t - 0.5)); // harmonics fall at 1/n^2: little aliasing
    case model::OscWave::Sine:
        return static_cast<float>(std::sin(kTwoPi * t));
    case model::OscWave::Noise:
        return white(noise);
    case model::OscWave::Off:
        break;
    }
    return 0.0f;
}

void SynthVoice::start(const model::SamplerParams& p, const model::SynthParams& s, int note, int from_note,
                       float velocity, float rate, std::uint32_t seed) {
    rate_ = rate;
    target_ = note;
    current_ = from_note >= 0 && s.glide_ms > 0.0f ? from_note : note;
    noise_ = seed | 1u;
    // Free-running unison phases (spread start phases avoid a phasey attack).
    for (std::size_t i = 0; i < ph1_.size(); ++i) {
        ph1_[i] = static_cast<double>(i) * 0.29;
        ph2_[i] = static_cast<double>(i) * 0.53;
    }
    sub_ = 0.0;
    lfo_ = 0.0;
    sh_ = 0.0f;
    chain_.start(p, note, velocity, rate);
}

float SynthVoice::lfo_value(const model::SynthParams& s) noexcept {
    switch (s.lfo_shape) {
    case model::LfoShape::Sine:
        return static_cast<float>(std::sin(kTwoPi * lfo_));
    case model::LfoShape::Triangle:
        return static_cast<float>(1.0 - 4.0 * std::fabs(lfo_ - 0.5));
    case model::LfoShape::Square:
        return lfo_ < 0.5 ? 1.0f : -1.0f;
    case model::LfoShape::SampleHold:
        return sh_;
    }
    return 0.0f;
}

void SynthVoice::render(float* left, float* right, std::size_t frames, const model::SamplerParams& p,
                        const model::SynthParams& s, double pitch, double bpm) noexcept {
    if (!active()) {
        return;
    }
    constexpr std::size_t kChunk = 32;
    const int unison = std::clamp(s.unison, 1, 4);
    // LFO rate: free, or synced to `lfo_sync` sixteenths per cycle.
    const double lfo_hz = s.lfo_sync > 0 ? bpm / 60.0 * 4.0 / s.lfo_sync : s.lfo_rate_hz;
    const double glide_coef = s.glide_ms > 0.0f ? std::exp(-1.0 / (s.glide_ms * 0.001 * rate_ / kChunk)) : 0.0;
    for (std::size_t c0 = 0; c0 < frames; c0 += kChunk) {
        const std::size_t n = std::min(kChunk, frames - c0);
        current_ = target_ + (current_ - target_) * glide_coef;
        const float lfo = lfo_value(s);
        const double old_lfo = lfo_;
        lfo_ = wrap(lfo_ + lfo_hz * static_cast<double>(n) / rate_);
        if (lfo_ < old_lfo) {
            sh_ = white(noise_); // new random value each cycle
        }
        chain_.chunk(p, s.lfo_to_cutoff * lfo);
        const double semis = current_ - 69.0 + p.fine_cents / 100.0 + s.lfo_to_pitch * lfo / 100.0;
        const double hz = 440.0 * std::exp2(semis / 12.0) * pitch;
        const float pw = std::clamp(s.pulse_width + s.lfo_to_pw * lfo, 0.05f, 0.95f);
        const float amp_mod = 1.0f - s.lfo_to_amp * (0.5f - 0.5f * lfo);
        const double inc_sub = hz * 0.5 / rate_;
        const float norm = 1.0f / std::sqrt(static_cast<float>(unison));
        std::array<double, 4> dt1{};
        std::array<double, 4> dt2{};
        const double osc2_ratio = std::exp2((s.osc2_semitones + s.osc2_detune / 100.0) / 12.0);
        for (int u = 0; u < unison; ++u) {
            const double cents = unison > 1 ? (u - (unison - 1) * 0.5) * s.unison_spread : 0.0;
            dt1[static_cast<std::size_t>(u)] = hz * std::exp2(cents / 1200.0) / rate_;
            dt2[static_cast<std::size_t>(u)] = dt1[static_cast<std::size_t>(u)] * osc2_ratio;
        }
        for (std::size_t i = c0; i < c0 + n; ++i) {
            float l = 0.0f;
            float r = 0.0f;
            for (int u = 0; u < unison; ++u) {
                const double d1 = dt1[static_cast<std::size_t>(u)];
                const double d2 = dt2[static_cast<std::size_t>(u)];
                auto& p1 = ph1_[static_cast<std::size_t>(u)];
                auto& p2 = ph2_[static_cast<std::size_t>(u)];
                const float v = s.osc1_level * osc(s.osc1, p1, d1, pw, noise_) + s.osc2_level * osc(s.osc2, p2, d2, pw, noise_);
                p1 = wrap(p1 + d1);
                p2 = wrap(p2 + d2);
                // Unison copies spread across the stereo field.
                const float pan = unison > 1 ? static_cast<float>(u) / static_cast<float>(unison - 1) * 2.0f - 1.0f : 0.0f;
                l += v * (1.0f - std::max(0.0f, pan) * 0.7f);
                r += v * (1.0f + std::min(0.0f, pan) * 0.7f);
            }
            l *= norm;
            r *= norm;
            if (s.sub_level > 0.0f) {
                const float sub = s.sub_level * osc(model::OscWave::Pulse, sub_, inc_sub, 0.5f, noise_);
                l += sub;
                r += sub;
            }
            sub_ = wrap(sub_ + inc_sub);
            if (s.noise_level > 0.0f) {
                const float nz = s.noise_level * white(noise_);
                l += nz;
                r += nz;
            }
            chain_.process(l * 0.4f, r * 0.4f, amp_mod, left[i], right[i]);
            if (!chain_.active()) {
                return;
            }
        }
    }
}

} // namespace phos::engine
