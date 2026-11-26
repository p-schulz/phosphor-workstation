#include "engine/snes_bus.h"

#include <algorithm>

namespace phos::engine {

void SnesBus::reset() {
    std::fill(echo_l_.begin(), echo_l_.end(), 0.0f);
    std::fill(echo_r_.begin(), echo_r_.end(), 0.0f);
    echo_pos_ = 0;
    fir_l_.fill(0.0f);
    fir_r_.fill(0.0f);
    fir_pos_ = 0;
    noise_level_ = -0x4000;
    noise_counter_ = 0;
    resampler_.reset();
}

const std::int16_t* SnesBus::noise(std::size_t n, int rate) noexcept {
    const int period = model::kSnesRates[static_cast<std::size_t>(std::clamp(rate, 0, 31))];
    for (std::size_t i = 0; i < n && i < kScratch; ++i) {
        if (period > 0 && --noise_counter_ <= 0) {
            noise_counter_ = period;
            // Level = ((Level SHR 1) AND 3FFFh) OR ((Level.Bit0 XOR Level.Bit1) SHL 14), 15-bit signed.
            const int u = noise_level_ & 0x7FFF;
            const int next = ((u >> 1) & 0x3FFF) | (((u ^ (u >> 1)) & 1) << 14);
            noise_level_ = next >= 0x4000 ? next - 0x8000 : next;
        }
        noise_[i] = static_cast<std::int16_t>(noise_level_);
    }
    return noise_.data();
}

void SnesBus::echo_frame(float in_l, float in_r, const model::SnesEcho& e, float& out_l, float& out_r) noexcept {
    // The oldest buffer entry goes into the FIR (fullsnes FIR formula; float, 15-bit scale 1.0).
    const std::size_t size = e.delay == 0 ? 1 : static_cast<std::size_t>(std::clamp(e.delay, 1, 15)) * 512;
    echo_pos_ %= size;
    fir_l_[fir_pos_] = echo_l_[echo_pos_];
    fir_r_[fir_pos_] = echo_r_[echo_pos_];
    float sum_l = 0.0f;
    float sum_r = 0.0f;
    for (std::size_t k = 0; k < 8; ++k) {
        // FIR0 weighs the oldest of the eight, FIR7 the newest.
        const std::size_t idx = (fir_pos_ + 1 + k) & 7;
        const float c = static_cast<float>(e.fir[k]) / 128.0f;
        sum_l += fir_l_[idx] * c;
        sum_r += fir_r_[idx] * c;
    }
    sum_l = std::clamp(sum_l, -1.0f, 1.0f); // last addition saturates
    sum_r = std::clamp(sum_r, -1.0f, 1.0f);
    fir_pos_ = (fir_pos_ + 1) & 7;
    out_l = sum_l * static_cast<float>(e.vol_l) / 128.0f;
    out_r = sum_r * static_cast<float>(e.vol_r) / 128.0f;
    echo_l_[echo_pos_] = std::clamp(in_l + sum_l * static_cast<float>(e.feedback) / 128.0f, -1.0f, 1.0f);
    echo_r_[echo_pos_] = std::clamp(in_r + sum_r * static_cast<float>(e.feedback) / 128.0f, -1.0f, 1.0f);
    ++echo_pos_;
}

void SnesBus::finish(std::size_t n, const model::SnesEcho& echo, float* out_l, float* out_r,
                     std::size_t device_frames) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        float el = 0.0f;
        float er = 0.0f;
        echo_frame(wet_l_[i], wet_r_[i], echo, el, er);
        dry_l_[i] += el;
        dry_r_[i] += er;
    }
    resampler_.push(dry_l_.data(), dry_r_.data(), n);
    resampler_.pull_add(out_l, out_r, device_frames);
}

} // namespace phos::engine
