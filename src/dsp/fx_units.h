#pragma once

#include "dsp/fx.h"
#include "dsp/svf.h"

// The effect units behind make_fx_unit() (PLAN.md §7.5). Internal to dsp/fx_*.cpp, dsp/plate.cpp
// and the tests.
namespace phos::dsp {

class ChorusFx final : public FxUnit {
public:
    explicit ChorusFx(float rate);
    void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept override;
    void reset() noexcept override;

private:
    DelayLine line_;
    float phase_ = 0.0f;
    float tone_l_ = 0.0f;
    float tone_r_ = 0.0f;
    Ramp mix_;
};

class TapeFx final : public FxUnit {
public:
    explicit TapeFx(float rate);
    void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept override;
    void reset() noexcept override;

private:
    DelayLine line_;
    DelayLine line_r_;
    float wow_phase_ = 0.0f;
    float flutter_phase_ = 0.0f;
    float flutter2_phase_ = 0.0f;
    float drift_ = 0.0f;
    float drift_target_ = 0.0f;
    int drift_count_ = 0;
    float lp_l_ = 0.0f;
    float lp_r_ = 0.0f;
    float hiss_lp_ = 0.0f;
    Noise noise_;
    Ramp mix_;
};

class CrushFx final : public FxUnit {
public:
    explicit CrushFx(float rate) : FxUnit(model::FxType::Crush, rate) {}
    void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept override;
    void reset() noexcept override { phase_ = 1.0f; hold_l_ = hold_r_ = 0.0f; }

private:
    float phase_ = 1.0f;
    float hold_l_ = 0.0f;
    float hold_r_ = 0.0f;
    Ramp mix_;
};

class LofiFx final : public FxUnit {
public:
    explicit LofiFx(float rate) : FxUnit(model::FxType::Lofi, rate) {}
    void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept override;
    void reset() noexcept override { svf_l_.reset(); svf_r_.reset(); }

private:
    Svf svf_l_;
    Svf svf_r_;
    Ramp cutoff_;
    Ramp mix_;
};

class DelayFx final : public FxUnit {
public:
    explicit DelayFx(float rate);
    void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept override;
    void reset() noexcept override;

private:
    DelayLine dl_;
    DelayLine dr_;
    float frames_ = -1.0f; // current delay (glides to a new tempo)
    float lp_l_ = 0.0f;
    float lp_r_ = 0.0f;
    Ramp mix_;
};

class DuckFx final : public FxUnit {
public:
    explicit DuckFx(float rate) : FxUnit(model::FxType::Duck, rate) {}
    void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept override;
    void reset() noexcept override { env_ = 0.0f; }
    float gain() const { return gain_; }

private:
    float env_ = 0.0f;
    float gain_ = 1.0f;
};

// Dattorro plate (dsp/plate.cpp).
class PlateFx final : public FxUnit {
public:
    explicit PlateFx(float rate);
    void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept override;
    void reset() noexcept override;

private:
    struct Allpass {
        DelayLine line;
        float length = 1.0f; // frames
        float process(float x, float g, float delay) noexcept {
            const float delayed = line.read(delay - 1.0f);
            const float w = x - g * delayed;
            line.push(w);
            return delayed + g * w;
        }
    };
    DelayLine predelay_;
    std::array<Allpass, 4> input_{};
    Allpass mod_l_;
    Allpass mod_r_;
    Allpass ap_l_;
    Allpass ap_r_;
    DelayLine del_l1_;
    DelayLine del_l2_;
    DelayLine del_r1_;
    DelayLine del_r2_;
    float len_l1_ = 0.0f;
    float len_l2_ = 0.0f;
    float len_r1_ = 0.0f;
    float len_r2_ = 0.0f;
    float scale_ = 1.0f;
    float bw_ = 0.0f;
    float damp_l_ = 0.0f;
    float damp_r_ = 0.0f;
    float end_l_ = 0.0f;
    float end_r_ = 0.0f;
    float lfo_ = 0.0f;
    Ramp mix_;
};

} // namespace phos::dsp
