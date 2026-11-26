// CRUSH, LOFI, DELAY, DUCK and the unit factory.

#include <cmath>
#include <numbers>

#include "dsp/fx_units.h"

namespace phos::dsp {

namespace {

float one_pole_coef(float hz, float rate) {
    return 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * std::min(hz, rate * 0.45f) / rate);
}

} // namespace

std::unique_ptr<FxUnit> make_fx_unit(model::FxType type, float sample_rate) {
    switch (type) {
    case model::FxType::Off:
        return nullptr;
    case model::FxType::Chorus:
        return std::make_unique<ChorusFx>(sample_rate);
    case model::FxType::Tape:
        return std::make_unique<TapeFx>(sample_rate);
    case model::FxType::Crush:
        return std::make_unique<CrushFx>(sample_rate);
    case model::FxType::Lofi:
        return std::make_unique<LofiFx>(sample_rate);
    case model::FxType::Plate:
        return std::make_unique<PlateFx>(sample_rate);
    case model::FxType::Delay:
        return std::make_unique<DelayFx>(sample_rate);
    case model::FxType::Duck:
        return std::make_unique<DuckFx>(sample_rate);
    }
    return nullptr;
}

// CRUSH: sample-and-hold at RATE (no anti-alias filter: the aliasing is the point), then
// quantisation to BITS (2^(bits-1) steps per polarity).
void CrushFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    const auto& p = ctx.slot->p;
    const float steps = std::exp2(std::round(p[0]) - 1.0f);
    const float inc = std::min(1.0f, p[1] / sample_rate());
    const float dmix = mix_.begin(p[4], frames);
    for (std::size_t i = 0; i < frames; ++i) {
        phase_ += inc;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            hold_l_ = std::round(l[i] * steps) / steps;
            hold_r_ = std::round(r[i] * steps) / steps;
        }
        mix_.value += dmix;
        const float m = mix_.value;
        l[i] = l[i] * (1.0f - m) + hold_l_ * m;
        r[i] = r[i] * (1.0f - m) + hold_r_ * m;
    }
}

// LOFI: resonant 12 dB low-pass (TPT SVF) with optional tanh drive in front.
void LofiFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    const auto& p = ctx.slot->p;
    const float dcut = cutoff_.begin(std::log2(p[0]), frames);
    const float drive = 1.0f + 4.0f * p[2];
    const float dmix = mix_.begin(p[4], frames);
    constexpr std::size_t kSub = 16; // coefficients every 16 samples
    for (std::size_t i = 0; i < frames; ++i) {
        if (i % kSub == 0) {
            const float hz = std::exp2(cutoff_.value + dcut * static_cast<float>(std::min(kSub, frames - i)));
            svf_l_.set(hz, p[1], sample_rate());
            svf_r_.set(hz, p[1], sample_rate());
        }
        cutoff_.value += dcut;
        float a = l[i];
        float b = r[i];
        if (p[2] > 0.001f) {
            a = std::tanh(drive * a) / drive;
            b = std::tanh(drive * b) / drive;
        }
        a = svf_l_.process(a, Svf::Mode::LowPass);
        b = svf_r_.process(b, Svf::Mode::LowPass);
        mix_.value += dmix;
        const float m = mix_.value;
        l[i] = l[i] * (1.0f - m) + a * m;
        r[i] = r[i] * (1.0f - m) + b * m;
    }
}

// DELAY: tempo-synced, a one-pole low-pass in the feedback path, optional ping-pong (the mono
// input enters the left line; each line feeds the other). A tempo change glides the delay time.
DelayFx::DelayFx(float rate) : FxUnit(model::FxType::Delay, rate) {
    const auto max = static_cast<std::size_t>(rate * 4.0f);
    dl_.allocate(max);
    dr_.allocate(max);
}

void DelayFx::reset() noexcept {
    dl_.clear();
    dr_.clear();
    frames_ = -1.0f;
    lp_l_ = lp_r_ = 0.0f;
}

void DelayFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    const auto& p = ctx.slot->p;
    const auto idx = static_cast<std::size_t>(std::clamp(static_cast<int>(p[0]), 0, 7));
    const double seconds = model::kDelayBeats[idx] * 60.0 / std::max(1.0, ctx.bpm);
    const float target = std::clamp(static_cast<float>(seconds * sample_rate()), 1.0f,
                                    static_cast<float>(dl_.capacity() - 2));
    if (frames_ < 0.0f) {
        frames_ = target;
    }
    const float glide = 1.0f - std::exp(-1.0f / (0.05f * sample_rate())); // ~50 ms
    const float fb = p[1];
    const float lp = one_pole_coef(p[2], sample_rate());
    const bool ping = p[3] >= 0.5f;
    const float dmix = mix_.begin(p[4], frames);
    for (std::size_t i = 0; i < frames; ++i) {
        frames_ += glide * (target - frames_);
        const float wl = dl_.read(frames_ - 1.0f);
        const float wr = dr_.read(frames_ - 1.0f);
        lp_l_ += lp * (wl - lp_l_);
        lp_r_ += lp * (wr - lp_r_);
        if (ping) {
            dl_.push(0.5f * (l[i] + r[i]) + fb * lp_r_);
            dr_.push(fb * lp_l_);
        } else {
            dl_.push(l[i] + fb * lp_l_);
            dr_.push(r[i] + fb * lp_r_);
        }
        mix_.value += dmix;
        const float m = mix_.value;
        l[i] = l[i] * (1.0f - m) + wl * m;
        r[i] = r[i] * (1.0f - m) + wr * m;
    }
}

// DUCK: a peak follower on the source track (1 ms attack, RELEASE) turns its level above THRESH
// into gain reduction, up to DEPTH (full depth 18 dB above the threshold).
void DuckFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    if (!ctx.side_l) {
        gain_ = 1.0f;
        return;
    }
    const auto& p = ctx.slot->p;
    const float attack = 1.0f - std::exp(-1.0f / (0.001f * sample_rate()));
    const float release = 1.0f - std::exp(-1.0f / (p[2] * 0.001f * sample_rate()));
    const float thresh = std::pow(10.0f, p[3] / 20.0f);
    for (std::size_t i = 0; i < frames; ++i) {
        const float s = std::max(std::fabs(ctx.side_l[i]), std::fabs(ctx.side_r[i]));
        env_ += (s > env_ ? attack : release) * (s - env_);
        const float over = env_ > thresh ? 20.0f * std::log10(env_ / thresh) / 18.0f : 0.0f;
        gain_ = 1.0f - p[1] * std::min(1.0f, over);
        l[i] *= gain_;
        r[i] *= gain_;
    }
}

} // namespace phos::dsp
