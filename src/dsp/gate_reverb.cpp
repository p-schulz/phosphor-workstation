// GATE: the gated reverb of 1980s drum sounds. Two ways it was made, both here:
// - GATED, the studio way: a big, bright room (here a 4-line feedback delay network, RT60 2.5 s)
//   whose output goes through a noise gate keyed from the dry signal. The gate opens on a hit,
//   holds for TIME and slams shut (1 ms attack, 6 ms release), so the room stops dead.
// - NONLIN / REVERSE, the digital-reverb way: no feedback at all. A dense cloud of 64 reflections
//   (diffused by four short allpasses) spread over TIME with a flat-ish (NONLIN) or rising (REVERSE)
//   envelope, and nothing after it.
// Generic techniques (allpass diffusion, Householder feedback matrix); no third-party code.

#include <cmath>
#include <numbers>

#include "dsp/fx_units.h"

namespace phos::dsp {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float kMaxTimeMs = 800.0f;
// Diffusers (ms, gain) and room lines (ms): mutually prime-ish lengths so echoes do not stack.
// Short and light, so the diffusion itself dies away within ~40 ms of TIME.
constexpr std::array<float, 4> kDiffuseMs = {1.1f, 1.7f, 2.9f, 4.1f};
constexpr std::array<float, 4> kDiffuseGain = {0.55f, 0.55f, 0.5f, 0.5f};
constexpr std::array<float, GateFx::kLines> kRoomMs = {31.0f, 37.0f, 41.1f, 47.3f};
constexpr float kRoomRt60 = 2.5f;     // s: the room keeps ringing behind the gate
constexpr float kRoomDampHz = 7000.0f;
constexpr float kCloudLevel = 1.5f;   // wet energy of the cloud relative to the input
constexpr float kRoomLevel = 1.2f;    // about as loud as the cloud (tests)

float one_pole_coef(float hz, float rate) { return 1.0f - std::exp(-kTwoPi * std::min(hz, rate * 0.45f) / rate); }
float time_coef(float ms, float rate) { return 1.0f - std::exp(-1.0f / (ms * 0.001f * rate)); }

// Envelope of the reflection cloud at fraction f of TIME.
float cloud_env(int mode, float f) { return mode == 2 ? 0.08f + 0.92f * f * f : 1.0f - 0.35f * f; }

} // namespace

GateFx::GateFx(float rate) : FxUnit(model::FxType::Gate, rate) {
    const float ms = rate * 0.001f;
    for (std::size_t i = 0; i < diffuse_.size(); ++i) {
        diffuse_[i].length = kDiffuseMs[i] * ms;
        diffuse_[i].g = kDiffuseGain[i];
        diffuse_[i].line.allocate(static_cast<std::size_t>(diffuse_[i].length) + 2);
    }
    cloud_.allocate(static_cast<std::size_t>((kMaxTimeMs + 10.0f) * ms));
    for (std::size_t i = 0; i < room_.size(); ++i) {
        room_len_[i] = std::round(kRoomMs[i] * ms);
        room_[i].allocate(static_cast<std::size_t>(room_len_[i]) + 2);
    }
    // Tap times: one per 1/64 of TIME, jittered within it; random signs. Fixed seeds, so every
    // unit sounds the same; left and right differ (width).
    Noise rnd;
    for (int side = 0; side < 2; ++side) {
        auto& pos = side == 0 ? pos_l_ : pos_r_;
        auto& sign = side == 0 ? sign_l_ : sign_r_;
        for (int i = 0; i < kTaps; ++i) {
            const float jitter = 0.4f * rnd.next(); // -0.4..0.4 of a tap spacing
            pos[static_cast<std::size_t>(i)] = std::clamp((static_cast<float>(i) + 0.5f + jitter) / kTaps, 0.01f, 1.0f);
            sign[static_cast<std::size_t>(i)] = rnd.next() < 0.0f ? -1.0f : 1.0f;
        }
    }
}

void GateFx::reset() noexcept {
    for (auto& a : diffuse_) {
        a.line.clear();
    }
    cloud_.clear();
    for (auto& d : room_) {
        d.clear();
    }
    room_lp_.fill(0.0f);
    tone_ = env_ = gate_ = hold_ = 0.0f;
}

void GateFx::process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept {
    const auto& p = ctx.slot->p;
    const int mode = std::clamp(static_cast<int>(p[0]), 0, 2);
    const float rate = sample_rate();
    const float target = std::clamp(p[1], 10.0f, kMaxTimeMs) * 0.001f * rate;
    time_frames_ = time_frames_ < 0.0f ? target : time_frames_ + 0.3f * (target - time_frames_); // glide per block
    const float threshold = std::pow(10.0f, p[2] / 20.0f);
    const float tone = one_pole_coef(p[3], rate);
    const float dmix = mix_.begin(p[4], frames);

    const bool cloud = mode != 0;
    if (cloud) {
        float energy = 0.0f;
        for (std::size_t i = 0; i < pos_l_.size(); ++i) {
            const float el = cloud_env(mode, pos_l_[i]);
            const float er = cloud_env(mode, pos_r_[i]);
            gain_l_[i] = sign_l_[i] * el;
            gain_r_[i] = sign_r_[i] * er;
            energy += 0.5f * (el * el + er * er);
        }
        const float norm = kCloudLevel / std::sqrt(energy);
        for (std::size_t i = 0; i < pos_l_.size(); ++i) {
            gain_l_[i] *= norm;
            gain_r_[i] *= norm;
        }
    }
    std::array<float, kLines> room_gain{};
    for (std::size_t k = 0; k < room_gain.size(); ++k) {
        room_gain[k] = std::pow(10.0f, -3.0f * room_len_[k] / (kRoomRt60 * rate)); // -60 dB per RT60
    }
    const float damp = one_pole_coef(kRoomDampHz, rate);
    const float env_release = 1.0f - time_coef(30.0f, rate);
    const float gate_open = time_coef(1.0f, rate);
    const float gate_close = time_coef(6.0f, rate);

    for (std::size_t i = 0; i < frames; ++i) {
        const float x = 0.5f * (l[i] + r[i]);
        tone_ += tone * (x - tone_);
        float d = tone_;
        for (auto& a : diffuse_) {
            d = a.process(d);
        }
        float wl = 0.0f;
        float wr = 0.0f;
        if (cloud) {
            cloud_.push(d);
            for (std::size_t k = 0; k < pos_l_.size(); ++k) {
                wl += gain_l_[k] * cloud_.read(pos_l_[k] * time_frames_);
                wr += gain_r_[k] * cloud_.read(pos_r_[k] * time_frames_);
            }
        } else {
            // Room: y = line outputs; feedback through the Householder matrix (I - 2/N 11^T),
            // damped, scaled to the RT60, plus the diffused input.
            std::array<float, kLines> y{};
            float sum = 0.0f;
            for (std::size_t k = 0; k < y.size(); ++k) {
                y[k] = room_[k].tap(static_cast<std::size_t>(room_len_[k]) - 1);
                sum += y[k];
            }
            for (std::size_t k = 0; k < y.size(); ++k) {
                const float fb = y[k] - 0.5f * sum;
                room_lp_[k] += damp * (fb - room_lp_[k]);
                room_[k].push(0.5f * d + room_gain[k] * room_lp_[k]);
            }
            // Gate keyed from the dry signal: open while it is above THRESH, then hold for TIME.
            const float a = std::fabs(x);
            env_ = a > env_ ? a : env_ * env_release;
            if (env_ > threshold) {
                hold_ = time_frames_;
            } else if (hold_ > 0.0f) {
                hold_ -= 1.0f;
            }
            const float want = hold_ > 0.0f ? 1.0f : 0.0f;
            gate_ += (want - gate_) * (want > gate_ ? gate_open : gate_close);
            wl = kRoomLevel * (y[0] - y[2] + 0.5f * y[1]) * gate_;
            wr = kRoomLevel * (y[1] - y[3] + 0.5f * y[2]) * gate_;
        }
        mix_.value += dmix;
        const float m = mix_.value;
        l[i] = l[i] * (1.0f - m) + wl * m;
        r[i] = r[i] * (1.0f - m) + wr * m;
    }
}

} // namespace phos::dsp
