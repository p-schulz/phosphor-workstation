// Modulated-delay effects: CHORUS (Juno-style) and TAPE (wow, flutter, saturation, hiss).

#include <cmath>
#include <numbers>

#include "dsp/fx_units.h"

namespace phos::dsp {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

float one_pole_coef(float hz, float rate) { return 1.0f - std::exp(-kTwoPi * std::min(hz, rate * 0.45f) / rate); }

} // namespace

// CHORUS: one BBD-like delay fed with the mono sum, read by two taps on opposite slopes of a
// triangle LFO (left +, right -), as the Juno-60 / 106 choruses do. Mode I 0.513 Hz and II
// 0.863 Hz sweep 1.66..5.35 ms; I+II is the fast shallow vibrato (9.75 Hz). Published service
// figures for those units; the circuit itself is not modelled.
ChorusFx::ChorusFx(float rate) : FxUnit(model::FxType::Chorus, rate) { line_.allocate(static_cast<std::size_t>(rate * 0.012f)); }

void ChorusFx::reset() noexcept {
    line_.clear();
    phase_ = 0.0f;
    tone_l_ = tone_r_ = 0.0f;
}

void ChorusFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    const auto& p = ctx.slot->p;
    const int mode = static_cast<int>(p[0]);
    const float depth = p[1];
    const float rate_hz = mode == 0 ? 0.513f : mode == 1 ? 0.863f : 9.75f;
    const float centre_ms = 3.505f;
    const float swing_ms = (mode == 2 ? 0.35f : 1.845f) * depth;
    const float ms = sample_rate() * 0.001f;
    const float inc = rate_hz / sample_rate();
    const float tone = one_pole_coef(p[2], sample_rate());
    const float dmix = mix_.begin(p[4], frames);
    for (std::size_t i = 0; i < frames; ++i) {
        line_.push(0.5f * (l[i] + r[i]));
        phase_ += inc;
        phase_ -= phase_ >= 1.0f ? 1.0f : 0.0f;
        const float tri = 4.0f * std::fabs(phase_ - 0.5f) - 1.0f;
        const float wl = line_.read((centre_ms + swing_ms * tri) * ms);
        const float wr = line_.read((centre_ms - swing_ms * tri) * ms);
        tone_l_ += tone * (wl - tone_l_);
        tone_r_ += tone * (wr - tone_r_);
        mix_.value += dmix;
        const float m = mix_.value;
        l[i] = l[i] * (1.0f - m) + tone_l_ * m;
        r[i] = r[i] * (1.0f - m) + tone_r_ * m;
    }
}

// TAPE: a short delay whose length wanders (wow: slow sine plus a smoothed random drift; flutter:
// two faster sines), then tanh saturation, a gentle head-loss low-pass and hiss.
TapeFx::TapeFx(float rate) : FxUnit(model::FxType::Tape, rate) {
    line_.allocate(static_cast<std::size_t>(rate * 0.02f));
    line_r_.allocate(static_cast<std::size_t>(rate * 0.02f));
}

void TapeFx::reset() noexcept {
    line_.clear();
    line_r_.clear();
    wow_phase_ = flutter_phase_ = flutter2_phase_ = 0.0f;
    drift_ = drift_target_ = 0.0f;
    drift_count_ = 0;
    lp_l_ = lp_r_ = hiss_lp_ = 0.0f;
}

void TapeFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    const auto& p = ctx.slot->p;
    const float ms = sample_rate() * 0.001f;
    const float wow_ms = 2.0f * p[0];
    const float flutter_ms = 0.12f * p[1];
    const float drive = p[2];
    const float gain = 1.0f + 5.0f * drive;
    const float hiss = 0.012f * p[3] * p[3];
    const float base_ms = 7.0f;
    const float lp = one_pole_coef(14000.0f - 6000.0f * drive, sample_rate());
    const float hiss_lp = one_pole_coef(7000.0f, sample_rate());
    const float drift_coef = one_pole_coef(0.7f, sample_rate());
    const float dmix = mix_.begin(p[4], frames);
    const float wow_inc = 0.55f / sample_rate();
    const float fl_inc = 7.3f / sample_rate();
    const float fl2_inc = 11.9f / sample_rate();
    for (std::size_t i = 0; i < frames; ++i) {
        line_.push(l[i]); // both channels share one modulation (one tape)
        line_r_.push(r[i]);
        wow_phase_ += wow_inc;
        wow_phase_ -= wow_phase_ >= 1.0f ? 1.0f : 0.0f;
        flutter_phase_ += fl_inc;
        flutter_phase_ -= flutter_phase_ >= 1.0f ? 1.0f : 0.0f;
        flutter2_phase_ += fl2_inc;
        flutter2_phase_ -= flutter2_phase_ >= 1.0f ? 1.0f : 0.0f;
        if (--drift_count_ <= 0) { // a new random drift target every ~0.4 s
            drift_target_ = noise_.next();
            drift_count_ = static_cast<int>(sample_rate() * 0.4f);
        }
        drift_ += drift_coef * (drift_target_ - drift_);
        const float wow = wow_ms * (0.7f * std::sin(kTwoPi * wow_phase_) + 0.3f * drift_);
        const float flutter =
            flutter_ms * (0.7f * std::sin(kTwoPi * flutter_phase_) + 0.3f * std::sin(kTwoPi * flutter2_phase_));
        const float d = (base_ms + wow + flutter) * ms;
        float a = line_.read(d);
        float b = line_r_.read(d);
        if (drive > 0.001f) {
            a = std::tanh(gain * a) / gain * (1.0f + drive);
            b = std::tanh(gain * b) / gain * (1.0f + drive);
        }
        lp_l_ += lp * (a - lp_l_);
        lp_r_ += lp * (b - lp_r_);
        hiss_lp_ += hiss_lp * (noise_.next() - hiss_lp_);
        const float h = hiss * hiss_lp_;
        mix_.value += dmix;
        const float m = mix_.value;
        l[i] = l[i] * (1.0f - m) + (lp_l_ + h) * m;
        r[i] = r[i] * (1.0f - m) + (lp_r_ + h) * m;
    }
}

} // namespace phos::dsp
