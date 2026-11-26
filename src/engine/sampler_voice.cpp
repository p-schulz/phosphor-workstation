#include "engine/sampler_voice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::engine {

namespace {

constexpr std::size_t kChunk = 32; // parameter / filter coefficient update interval

float db_gain(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

void SamplerVoice::start(const model::SampleData* sample, int slot, const model::SamplerParams& p, int note,
                         float velocity, float engine_rate) noexcept {
    sample_ = sample;
    slot_ = slot;
    note_ = note;
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    engine_rate_ = engine_rate;
    pos_ = 0.0;
    dir_ = 1;
    amp_.kill();
    fenv_.kill();
    amp_.set(p.amp.attack_ms, p.amp.decay_ms, p.amp.sustain, p.amp.release_ms, engine_rate);
    fenv_.set(p.filter_env.attack_ms, p.filter_env.decay_ms, p.filter_env.sustain, p.filter_env.release_ms, engine_rate);
    amp_.gate_on();
    fenv_.gate_on();
    svf_l_.reset();
    svf_r_.reset();
    update_loop(p);
}

void SamplerVoice::release() noexcept {
    amp_.gate_off();
    fenv_.gate_off();
}

void SamplerVoice::kill() noexcept {
    amp_.kill();
    sample_ = nullptr;
}

void SamplerVoice::update_loop(const model::SamplerParams& p) noexcept {
    const auto frames = static_cast<long>(sample_->frames);
    loop_start_ = std::clamp(static_cast<long>(p.loop_start), 0L, frames);
    loop_end_ = std::clamp(static_cast<long>(p.loop_end), 0L, frames);
    looping_ = p.loop != model::LoopMode::Off && loop_end_ - loop_start_ >= 2;
    ping_pong_ = p.loop == model::LoopMode::PingPong;
}

float SamplerVoice::fetch(const float* data, long i) const noexcept {
    const auto frames = static_cast<long>(sample_->frames);
    if (looping_ && i >= loop_end_) {
        const long len = loop_end_ - loop_start_;
        if (ping_pong_) {
            const long k = (i - loop_end_) % (2 * len); // mirror at loop_end, then at loop_start
            i = k < len ? loop_end_ - 1 - k : loop_start_ + (k - len);
        } else {
            i = loop_start_ + (i - loop_start_) % len;
        }
    }
    return i >= 0 && i < frames ? data[i] : 0.0f;
}

float SamplerVoice::interpolate(const float* d, double pos, model::Interp interp) const noexcept {
    const double fl = std::floor(pos);
    const auto i = static_cast<long>(fl);
    const auto t = static_cast<float>(pos - fl);
    switch (interp) {
    case model::Interp::Drop:
        return fetch(d, i);
    case model::Interp::Linear: {
        const float a = fetch(d, i);
        return a + (fetch(d, i + 1) - a) * t;
    }
    case model::Interp::Hermite:
        break;
    }
    // 4-point, 3rd-order Hermite (Catmull-Rom form, as in L. de Soras, "Polynomial interpolators
    // for high-quality resampling of oversampled audio", 2001).
    const float xm1 = fetch(d, i - 1);
    const float x0 = fetch(d, i);
    const float x1 = fetch(d, i + 1);
    const float x2 = fetch(d, i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void SamplerVoice::render(float* left, float* right, std::size_t frames, const model::SamplerParams& p,
                          double pitch) noexcept {
    if (!active()) {
        return;
    }
    update_loop(p);
    amp_.set(p.amp.attack_ms, p.amp.decay_ms, p.amp.sustain, p.amp.release_ms, engine_rate_);
    fenv_.set(p.filter_env.attack_ms, p.filter_env.decay_ms, p.filter_env.sustain, p.filter_env.release_ms,
              engine_rate_);

    const model::SampleData& s = *sample_;
    const float* dl = s.data(0);
    const float* dr = s.data(1);
    const bool stereo = s.channels == 2;
    const double inc = static_cast<double>(s.sample_rate) / engine_rate_ *
                       std::exp2((note_ - p.root_note) / 12.0 + p.fine_cents / 1200.0) * pitch;
    const float vel_gain = 1.0f - p.vel_to_amp + p.vel_to_amp * velocity_;
    const float gain = vel_gain * db_gain(p.volume_db);
    // Mono: equal-power pan normalised to unity in the centre. Stereo: balance.
    float pl = 1.0f;
    float pr = 1.0f;
    if (stereo) {
        pl = std::min(1.0f, 1.0f - p.pan);
        pr = std::min(1.0f, 1.0f + p.pan);
    } else {
        const float theta = (p.pan + 1.0f) * std::numbers::pi_v<float> / 4.0f;
        pl = std::cos(theta) * std::numbers::sqrt2_v<float>;
        pr = std::sin(theta) * std::numbers::sqrt2_v<float>;
    }
    const bool filtered = p.filter != model::FilterType::Off;
    const auto mode = p.filter == model::FilterType::HighPass  ? dsp::Svf::Mode::HighPass
                      : p.filter == model::FilterType::BandPass ? dsp::Svf::Mode::BandPass
                                                                 : dsp::Svf::Mode::LowPass;
    const auto end = static_cast<double>(s.frames);

    for (std::size_t c0 = 0; c0 < frames && active(); c0 += kChunk) {
        const std::size_t n = std::min(kChunk, frames - c0);
        if (filtered) {
            const float octaves = p.env_amount * fenv_.level() + p.key_track * static_cast<float>(note_ - 60) / 12.0f +
                                  p.vel_to_cutoff * velocity_;
            const float fc = p.cutoff_hz * std::exp2(octaves);
            svf_l_.set(fc, p.resonance, engine_rate_);
            svf_r_.set(fc, p.resonance, engine_rate_);
        }
        for (std::size_t i = c0; i < c0 + n; ++i) {
            if (!looping_ && pos_ >= end) {
                kill(); // one-shot reached the end
                return;
            }
            const float env = amp_.next() * gain;
            fenv_.next();
            float l = interpolate(dl, pos_, p.interp);
            float r = stereo ? interpolate(dr, pos_, p.interp) : l;
            if (filtered) {
                l = svf_l_.process(l, mode);
                r = stereo ? svf_r_.process(r, mode) : l;
            }
            left[i] += l * env * pl;
            right[i] += r * env * pr;
            if (!amp_.active()) {
                sample_ = nullptr;
                return;
            }

            if (!ping_pong_ || !looping_) {
                pos_ += inc;
                if (looping_ && pos_ >= static_cast<double>(loop_end_)) {
                    // fmod: at extreme pitches one step can be longer than the loop.
                    pos_ = static_cast<double>(loop_start_) +
                           std::fmod(pos_ - static_cast<double>(loop_start_), static_cast<double>(loop_end_ - loop_start_));
                }
                continue;
            }
            // Ping-pong: reflect at both loop ends.
            pos_ += inc * dir_;
            const auto lo = static_cast<double>(loop_start_);
            const auto hi = static_cast<double>(loop_end_ - 1);
            if (dir_ > 0 && pos_ >= hi) {
                pos_ = std::clamp(2.0 * hi - pos_, lo, hi);
                dir_ = -1;
            } else if (dir_ < 0 && pos_ <= lo) {
                pos_ = std::clamp(2.0 * lo - pos_, lo, hi);
                dir_ = 1;
            }
        }
    }
}

} // namespace phos::engine
