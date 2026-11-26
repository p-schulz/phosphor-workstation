// PLATE: the plate-class reverberator of J. Dattorro, "Effect Design Part 1: Reverberator and
// Other Filters", JAES 45(9), 1997, Fig. 1 and Tables 1-2. Delay lengths there are given at
// 29761 Hz and are scaled to the engine rate here. Implemented from the paper's figure and
// tables; no third-party code.

#include <cmath>
#include <numbers>

#include "dsp/fx_units.h"

namespace phos::dsp {

namespace {

constexpr float kPaperRate = 29761.0f;
// Table 1: input diffusers, then the tank (left: 672 mod. allpass, 4453 delay, 1800 allpass,
// 3720 delay; right: 908, 4217, 2656, 3163). Excursion 16 samples.
constexpr std::array<float, 4> kInput = {142.0f, 107.0f, 379.0f, 277.0f};
constexpr float kModL = 672.0f;
constexpr float kModR = 908.0f;
constexpr float kDelL1 = 4453.0f;
constexpr float kDelL2 = 3720.0f;
constexpr float kApL = 1800.0f;
constexpr float kDelR1 = 4217.0f;
constexpr float kDelR2 = 3163.0f;
constexpr float kApR = 2656.0f;
constexpr float kExcursion = 16.0f;
constexpr float kInputDiffusion1 = 0.75f;
constexpr float kInputDiffusion2 = 0.625f;
constexpr float kDecayDiffusion1 = 0.70f;
constexpr float kBandwidth = 0.9995f;

std::size_t frames_for(float paper_len, float scale) { return static_cast<std::size_t>(paper_len * scale) + 64; }

} // namespace

PlateFx::PlateFx(float rate) : FxUnit(model::FxType::Plate, rate), scale_(rate / kPaperRate) {
    predelay_.allocate(static_cast<std::size_t>(rate * 0.21f));
    for (std::size_t i = 0; i < input_.size(); ++i) {
        input_[i].length = kInput[i] * scale_;
        input_[i].line.allocate(frames_for(kInput[i], scale_));
    }
    mod_l_.length = kModL * scale_;
    mod_l_.line.allocate(frames_for(kModL + kExcursion, scale_));
    mod_r_.length = kModR * scale_;
    mod_r_.line.allocate(frames_for(kModR + kExcursion, scale_));
    ap_l_.length = kApL * scale_;
    ap_l_.line.allocate(frames_for(kApL, scale_));
    ap_r_.length = kApR * scale_;
    ap_r_.line.allocate(frames_for(kApR, scale_));
    len_l1_ = kDelL1 * scale_;
    len_l2_ = kDelL2 * scale_;
    len_r1_ = kDelR1 * scale_;
    len_r2_ = kDelR2 * scale_;
    del_l1_.allocate(frames_for(kDelL1, scale_));
    del_l2_.allocate(frames_for(kDelL2, scale_));
    del_r1_.allocate(frames_for(kDelR1, scale_));
    del_r2_.allocate(frames_for(kDelR2, scale_));
}

void PlateFx::reset() noexcept {
    predelay_.clear();
    for (auto& a : input_) {
        a.line.clear();
    }
    for (Allpass* a : {&mod_l_, &mod_r_, &ap_l_, &ap_r_}) {
        a->line.clear();
    }
    for (DelayLine* d : {&del_l1_, &del_l2_, &del_r1_, &del_r2_}) {
        d->clear();
    }
    bw_ = damp_l_ = damp_r_ = end_l_ = end_r_ = lfo_ = 0.0f;
}

void PlateFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    const auto& p = ctx.slot->p;
    const float decay = p[0];
    const float damping = 0.9f * p[1];
    const float pre = std::max(1.0f, p[2] * 0.001f * sample_rate());
    const float width = p[3];
    const float decay_diffusion2 = std::clamp(decay + 0.15f, 0.25f, 0.5f); // per the paper
    const float exc = kExcursion * scale_;
    const float lfo_inc = 2.0f * std::numbers::pi_v<float> * 1.0f / sample_rate(); // ~1 Hz
    const float dmix = mix_.begin(p[4], frames);
    const float s = scale_;
    // Output taps (Table 2), in paper samples.
    const auto tap = [s](const DelayLine& d, float n) { return d.tap(static_cast<std::size_t>(n * s)); };
    for (std::size_t i = 0; i < frames; ++i) {
        predelay_.push(0.5f * (l[i] + r[i]));
        const float x = predelay_.read(pre - 1.0f);
        bw_ += kBandwidth * (x - bw_);
        float d = bw_;
        d = input_[0].process(d, kInputDiffusion1, input_[0].length);
        d = input_[1].process(d, kInputDiffusion1, input_[1].length);
        d = input_[2].process(d, kInputDiffusion2, input_[2].length);
        d = input_[3].process(d, kInputDiffusion2, input_[3].length);

        lfo_ += lfo_inc;
        lfo_ -= lfo_ > 6.2831853f ? 6.2831853f : 0.0f;
        const float in_l = d + decay * end_r_;
        const float in_r = d + decay * end_l_;

        float a = mod_l_.process(in_l, -kDecayDiffusion1, mod_l_.length + exc * std::sin(lfo_));
        del_l1_.push(a);
        a = del_l1_.read(len_l1_ - 1.0f);
        damp_l_ = a * (1.0f - damping) + damp_l_ * damping;
        a = ap_l_.process(damp_l_ * decay, decay_diffusion2, ap_l_.length);
        del_l2_.push(a);

        float b = mod_r_.process(in_r, -kDecayDiffusion1, mod_r_.length + exc * std::cos(lfo_));
        del_r1_.push(b);
        b = del_r1_.read(len_r1_ - 1.0f);
        damp_r_ = b * (1.0f - damping) + damp_r_ * damping;
        b = ap_r_.process(damp_r_ * decay, decay_diffusion2, ap_r_.length);
        del_r2_.push(b);

        end_l_ = del_l2_.read(len_l2_ - 1.0f);
        end_r_ = del_r2_.read(len_r2_ - 1.0f);

        // Node names of Fig. 1: 24_30 = del_l1, 31_33 = ap_l, 33_39 = del_l2, 48_54 = del_r1,
        // 55_59 = ap_r, 59_63 = del_r2.
        const float yl = 0.6f * (tap(del_r1_, 266) + tap(del_r1_, 2974) - tap(ap_r_.line, 1913) + tap(del_r2_, 1996) -
                                 tap(del_l1_, 1990) - tap(ap_l_.line, 187) - tap(del_l2_, 1066));
        const float yr = 0.6f * (tap(del_l1_, 353) + tap(del_l1_, 3627) - tap(ap_l_.line, 1228) + tap(del_l2_, 2673) -
                                 tap(del_r1_, 2111) - tap(ap_r_.line, 335) - tap(del_r2_, 121));
        const float mid = 0.5f * (yl + yr);
        const float side = 0.5f * (yl - yr) * width;
        mix_.value += dmix;
        const float m = mix_.value;
        l[i] = l[i] * (1.0f - m) + (mid + side) * m;
        r[i] = r[i] * (1.0f - m) + (mid - side) * m;
    }
}

} // namespace phos::dsp
