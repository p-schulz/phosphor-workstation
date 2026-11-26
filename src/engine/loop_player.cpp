#include "engine/loop_player.h"

#include <algorithm>
#include <cmath>

namespace phos::engine {

namespace {

constexpr float kLaunchFadeS = 0.002f;
constexpr float kStopFadeS = 0.0015f; // short: a stop on a beat must not leak that beat
constexpr float kSliceFadeS = 0.002f;
constexpr float kLpOpenHz = 19990.0f;

const float* channel(const model::SampleData& s, int c) { return s.data(c); }

// 4-point Hermite on channel `c` at fractional position `x`; `wrap` reads across the loop end.
float hermite(const model::SampleData& s, int c, double x, bool wrap) noexcept {
    const auto len = static_cast<std::int64_t>(s.frames);
    const auto i = static_cast<std::int64_t>(std::floor(x));
    const auto t = static_cast<float>(x - static_cast<double>(i));
    const float* p = channel(s, c);
    const auto at = [&](std::int64_t k) {
        if (wrap) {
            k %= len;
            return p[k < 0 ? k + len : k];
        }
        return k < 0 || k >= len ? 0.0f : p[k];
    };
    const float xm1 = at(i - 1);
    const float x0 = at(i);
    const float x1 = at(i + 1);
    const float x2 = at(i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

} // namespace

void LoopPlayer::prepare(float sample_rate) noexcept {
    rate_ = sample_rate;
    for (Slot& s : slots_) {
        s.gain.prepare(sample_rate, 5.0f);
        s.state = LoopState::Off;
        s.lp_l.reset();
        s.lp_r.reset();
    }
}

void LoopPlayer::launch(int slot, std::uint64_t at) noexcept {
    if (slot < 0 || slot >= kSlots) {
        return;
    }
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    if (s.state == LoopState::Off) {
        s.state = LoopState::Pending;
    }
    s.start = at; // a playing slot keeps playing until `at`, then restarts at beat 1
    s.stop_at = UINT64_MAX;
    s.stopping = false;
}

void LoopPlayer::stop(int slot, std::uint64_t at) noexcept {
    if (slot < 0 || slot >= kSlots) {
        return;
    }
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    if (s.state == LoopState::Pending) {
        s.state = LoopState::Off;
        return;
    }
    s.stop_at = at;
}

void LoopPlayer::stop_all(std::uint64_t at) noexcept {
    for (int i = 0; i < kSlots; ++i) {
        stop(i, at);
    }
}

void LoopPlayer::kill_all() noexcept {
    for (Slot& s : slots_) {
        s.state = LoopState::Off;
    }
}

float LoopPlayer::frame_out(const LoopBlock& b, Slot& s, int bpm10, float& right) noexcept {
    const model::SampleData* src = b.sample;
    if (b.sync == model::LoopSync::Stretch && b.stretched && b.stretch_bpm10 == bpm10) {
        src = b.stretched;
    }
    const auto len = static_cast<double>(src->frames);
    const int rc = src->channels == 2 ? 1 : 0;
    if (b.sync != model::LoopSync::Slice) {
        double pos = s.phase / s.beats * len;
        if (b.reverse) {
            pos = len - 1.0 - pos;
        }
        right = hermite(*src, rc, pos, true);
        return hermite(*src, 0, pos, true);
    }
    // SLICE: the source plays at its own speed from the slice start; the tempo only decides when.
    const int slices = std::max(1, b.slices);
    const double slice_beats = s.beats / slices;
    const double k = std::min(std::floor(s.phase / slice_beats), static_cast<double>(slices - 1));
    const double t = s.phase - k * slice_beats;                 // beats into the slice
    const double beat_frames = 600.0 * rate_ / std::max(1, bpm10); // output frames per beat at screw 1
    const double slice_frames = len / slices;
    const double off = t * beat_frames;
    if (off >= slice_frames) {
        right = 0.0f;
        return 0.0f;
    }
    const double fade = kSliceFadeS * rate_;
    const double remaining = (slice_beats - t) * beat_frames;
    const auto env = static_cast<float>(std::min({1.0, off / fade, (slice_frames - off) / fade, remaining / fade}));
    double pos = k * slice_frames + off;
    if (b.reverse) {
        pos = len - 1.0 - pos;
    }
    right = env * hermite(*src, rc, pos, false);
    return env * hermite(*src, 0, pos, false);
}

void LoopPlayer::render(std::span<const LoopBlock> blocks, std::span<float* const> lefts,
                        std::span<float* const> rights, std::uint64_t frame, std::uint32_t n, int bpm10,
                        float speed) noexcept {
    const float launch_step = 1.0f / (kLaunchFadeS * rate_);
    const float stop_step = 1.0f / (kStopFadeS * rate_);
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = slots_[static_cast<std::size_t>(i)];
        if (s.state == LoopState::Off) {
            continue;
        }
        const LoopBlock& b = blocks[static_cast<std::size_t>(i)];
        if (!b.sample || b.sample->frames < 4 || b.beats <= 0.0) {
            s.state = LoopState::Off;
            continue;
        }
        if (s.beats != b.beats) { // BARS edited while playing: keep the relative position
            s.phase = s.phase / s.beats * b.beats;
            s.beats = b.beats;
        }
        const auto bus = static_cast<std::size_t>(std::clamp(b.track, 0, static_cast<int>(lefts.size()) - 1));
        float* out_l = lefts[bus];
        float* out_r = rights[bus];
        s.gain.set_target(b.gain);
        const bool filter = b.lp_hz < kLpOpenHz;
        if (filter) {
            s.lp_l.set(b.lp_hz, 0.1f, rate_);
            s.lp_r.set(b.lp_hz, 0.1f, rate_);
        }
        const double dphase = bpm10 / 600.0 / rate_ * b.screw * speed;
        for (std::uint32_t j = 0; j < n; ++j) {
            const std::uint64_t f = frame + j;
            if (f < s.start && s.state == LoopState::Pending) {
                continue;
            }
            if (s.start != UINT64_MAX && f >= s.start) {
                s.state = LoopState::Playing;
                s.phase = 0.0;
                s.beats = b.beats;
                s.fade = 0.0f;
                s.start = UINT64_MAX; // done
            }
            if (f >= s.stop_at) {
                s.stopping = true;
            }
            if (s.stopping) {
                s.fade -= stop_step;
                if (s.fade <= 0.0f) {
                    s.state = LoopState::Off;
                    s.stopping = false;
                    s.stop_at = UINT64_MAX;
                    break;
                }
            } else {
                s.fade = std::min(1.0f, s.fade + launch_step);
            }
            float r = 0.0f;
            float l = frame_out(b, s, bpm10, r);
            const float g = s.gain.next() * s.fade;
            l *= g;
            r *= g;
            if (filter) {
                l = s.lp_l.process(l, dsp::Svf::Mode::LowPass);
                r = s.lp_r.process(r, dsp::Svf::Mode::LowPass);
            }
            out_l[j] += l;
            out_r[j] += r;
            s.phase += dphase;
            if (s.phase >= s.beats) {
                s.phase -= s.beats;
            }
        }
    }
}

void LoopPlayer::publish(std::array<std::atomic<std::uint8_t>, kSlots>& states,
                         std::array<std::atomic<float>, kSlots>& phases) const noexcept {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        states[i].store(static_cast<std::uint8_t>(slots_[i].state), std::memory_order_relaxed);
        phases[i].store(static_cast<float>(slots_[i].phase / std::max(1e-9, slots_[i].beats)), std::memory_order_relaxed);
    }
}

} // namespace phos::engine
