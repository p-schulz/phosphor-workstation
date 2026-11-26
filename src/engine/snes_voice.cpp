#include "engine/snes_voice.h"

#include <algorithm>
#include <cmath>

namespace phos::engine {

int SnesVoice::pitch_register(int source_rate, int note, int root, float fine_cents, double bend) {
    const double ratio = static_cast<double>(source_rate) / 32000.0 *
                         std::exp2((note - root) / 12.0 + fine_cents / 1200.0) * bend;
    return std::clamp(static_cast<int>(std::lround(0x1000 * ratio)), 0, 0x3FFF);
}

void SnesVoice::start(const model::BrrSample* brr, const model::SnesParams& p, int note, float fine_cents, float velocity) {
    brr_ = brr && brr->blocks() > 0 ? brr : nullptr;
    note_ = note;
    fine_ = fine_cents;
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    block_ = -1;
    counter_ = 0;
    std::fill(std::begin(buf_), std::end(buf_), std::int16_t{0});
    old_ = older_ = 0;
    end_after_block_ = false;
    env_ = 0;
    stage_ = Stage::Attack;
    rate_counter_ = 0;
    delay_ = 5;
    last_out_ = 0;
    (void)p;
    if (brr_) {
        next_block();
    }
}

void SnesVoice::next_block() noexcept {
    if (end_after_block_) {
        // The previous block carried the End flag: loop or stop (fullsnes "Loop/End bits").
        if (brr_->loop_block < 0) {
            env_ = 0; // Code 1 = End+Mute: release with Env = 0
            brr_ = nullptr;
            return;
        }
        block_ = brr_->loop_block;
    } else {
        ++block_;
    }
    // Keep the last three samples as Gaussian history.
    buf_[0] = buf_[16];
    buf_[1] = buf_[17];
    buf_[2] = buf_[18];
    const std::uint8_t* b = brr_->data.data() + static_cast<std::size_t>(block_) * 9;
    model::brr_decode_block(b, buf_ + 3, old_, older_);
    end_after_block_ = (b[0] & 1) != 0 || block_ + 1 >= static_cast<int>(brr_->blocks());
}

void SnesVoice::envelope(const model::SnesParams& p) noexcept {
    auto step_due = [&](int rate) {
        const int period = model::kSnesRates[static_cast<std::size_t>(std::clamp(rate, 0, 31))];
        if (period == 0) {
            return false;
        }
        if (--rate_counter_ > 0) {
            return false;
        }
        rate_counter_ = period;
        return true;
    };
    auto exp_down = [&] { env_ -= ((env_ - 1) >> 8) + 1; };
    if (stage_ == Stage::Release) {
        env_ = std::max(0, env_ - 8); // Rate=31, Step=-8 every sample
        if (env_ == 0) {
            brr_ = nullptr;
        }
        return;
    }
    if (p.adsr) {
        switch (stage_) {
        case Stage::Attack: {
            const int rate = p.attack * 2 + 1;
            if (step_due(rate)) {
                env_ += rate == 31 ? 1024 : 32;
            }
            if (env_ >= 0x7E0) {
                env_ = std::min(env_, 0x7FF);
                stage_ = Stage::Decay;
            }
            break;
        }
        case Stage::Decay:
            if (step_due(p.decay * 2 + 16)) {
                exp_down();
            }
            if (env_ <= (p.sustain_level + 1) * 0x100) {
                stage_ = Stage::Sustain;
            }
            break;
        case Stage::Sustain:
            if (step_due(p.sustain_rate)) {
                exp_down();
            }
            break;
        case Stage::Release:
            break;
        }
    } else if (p.gain_mode == model::SnesGainMode::Direct) {
        env_ = std::clamp(p.gain, 0, 127) * 16;
    } else if (step_due(p.gain & 31)) {
        switch (p.gain_mode) {
        case model::SnesGainMode::LinearDecrease:
            env_ -= 32;
            break;
        case model::SnesGainMode::ExpDecrease:
            exp_down();
            break;
        case model::SnesGainMode::LinearIncrease:
            env_ += 32;
            break;
        case model::SnesGainMode::BentIncrease:
            env_ += env_ < 0x600 ? 32 : 8;
            break;
        case model::SnesGainMode::Direct:
            break;
        }
    }
    env_ = std::clamp(env_, 0, 0x7FF); // "clip E to 0 or 0x7ff rather than wrapping"
}

int SnesVoice::sample(const model::SnesParams& p, double bend, int prev_out, int noise, float& l, float& r) noexcept {
    if (!brr_) {
        return 0;
    }
    if (delay_ > 0) {
        --delay_;
        return 0;
    }
    envelope(p);
    if (!brr_) {
        return 0;
    }
    int out = 0;
    if (p.noise) {
        out = noise; // Gaussian interpolation isn't applied on noise
    } else {
        // Gaussian interpolation (fullsnes "4-Point Gaussian Interpolation").
        const int i = static_cast<int>((counter_ >> 4) & 0xFF);
        const int s = static_cast<int>(counter_ >> 12) & 15;
        const int newest = buf_[3 + s];
        const int old = buf_[3 + s - 1];
        const int older = buf_[3 + s - 2];
        const int oldest = buf_[3 + s - 3];
        const auto& g = model::kSnesGauss;
        int sum = (g[static_cast<std::size_t>(0xFF - i)] * oldest) >> 10;
        sum += (g[static_cast<std::size_t>(0x1FF - i)] * older) >> 10;
        sum += (g[static_cast<std::size_t>(0x100 + i)] * old) >> 10;
        sum = static_cast<std::int16_t>(sum); // no overflow handling for these additions
        sum += (g[static_cast<std::size_t>(i)] * newest) >> 10;
        sum = std::clamp(sum, -0x8000, 0x7FFF); // the last one saturates
        out = sum >> 1;
    }
    out = (out * env_) >> 11;
    last_out_ = out;

    // Pitch counter (fullsnes "Pitch Counter").
    int step = pitch_register(brr_->source_rate, note_, p.root_note, fine_, bend);
    if (p.pitch_mod) {
        const int factor = (prev_out >> 4) + 0x400;
        step = (step * factor) >> 10;
    }
    counter_ += static_cast<std::uint32_t>(std::min(step, 0x3FFF));
    while (counter_ >= 0x10000 && brr_) { // bounded: step <= 0x3FFF crosses at most one block
        counter_ -= 0x10000;
        next_block();
    }
    l = static_cast<float>(out) / 16384.0f * (static_cast<float>(p.vol_l) / 128.0f) * velocity_;
    r = static_cast<float>(out) / 16384.0f * (static_cast<float>(p.vol_r) / 128.0f) * velocity_;
    return out;
}

} // namespace phos::engine
