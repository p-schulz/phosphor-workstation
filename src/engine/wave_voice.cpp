#include "engine/wave_voice.h"

#include <algorithm>
#include <cmath>

namespace phos::engine {

void WaveVoice::start(const WaveTables* tables, const model::SamplerParams& p, int note, float velocity, float rate) {
    tables_ = tables && tables->segments > 0 ? tables : nullptr;
    note_ = note;
    rate_ = rate;
    phase_ = 0.0;
    segment_ = 0;
    cycles_done_ = 0;
    chain_.start(p, note, velocity, rate);
}

int WaveVoice::next_segment(int s, const model::WavetableParams& w) const {
    const int n = tables_->segments;
    const int loop_start = std::clamp(w.loop_start, 0, n - 1);
    const int loop_len = std::clamp(w.loop_length, 0, n - loop_start);
    if (loop_len > 0 && s + 1 >= loop_start + loop_len) {
        return loop_start;
    }
    return std::min(s + 1, n - 1);
}

void WaveVoice::render(float* left, float* right, std::size_t frames, const model::SamplerParams& p,
                       const model::WavetableParams& w, double pitch) noexcept {
    if (!active()) {
        return;
    }
    constexpr std::size_t kChunk = 32;
    const double hz = 440.0 * std::exp2((note_ - 69) / 12.0 + p.fine_cents / 1200.0) * pitch;
    const double inc = hz / rate_;
    const int level = w.authentic ? 0 : WaveTables::level_for(hz, rate_ * 0.5);
    for (std::size_t c0 = 0; c0 < frames; c0 += kChunk) {
        const std::size_t n = std::min(kChunk, frames - c0);
        chain_.chunk(p);
        for (std::size_t i = c0; i < c0 + n; ++i) {
            const int s = std::min(segment_, tables_->segments - 1);
            const int cycles = tables_->cycles[static_cast<std::size_t>(s)];
            const double pos = phase_ * WaveTables::kPoints;
            const auto idx = static_cast<int>(pos);
            const auto frac = static_cast<float>(pos - idx);
            const float* a = tables_->table(s, level);
            float v = a[idx] + (a[idx + 1] - a[idx]) * frac;
            if (w.crossfade) {
                const int next = next_segment(s, w);
                if (next != s) {
                    const float* b = tables_->table(next, level);
                    const float vb = b[idx] + (b[idx + 1] - b[idx]) * frac;
                    const auto t = static_cast<float>((cycles_done_ + phase_) / cycles);
                    v += (vb - v) * t;
                }
            }
            chain_.process(v, v, 1.0f, left[i], right[i]);
            if (!chain_.active()) {
                tables_ = nullptr;
                return;
            }
            phase_ += inc;
            while (phase_ >= 1.0) { // bounded: inc < 1 below Nyquist
                phase_ -= 1.0;
                if (++cycles_done_ >= cycles) {
                    cycles_done_ = 0;
                    segment_ = next_segment(s, w);
                }
            }
        }
    }
}

} // namespace phos::engine
