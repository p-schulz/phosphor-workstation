#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "model/fx.h"

// Effect processors (PLAN.md §7.5). A unit is created (and its delay lines allocated) on the UI
// thread for one type and sample rate, handed to the engine inside a snapshot, and from then on
// touched only by the audio thread: process() never allocates. Parameters arrive with every
// block; units smooth the ones that would click.
namespace phos::dsp {

struct FxContext {
    const model::FxSlot* slot = nullptr;
    double bpm = 120.0;
    // DUCK: the source track's signal for this block (pre-fader), or nullptr.
    const float* side_l = nullptr;
    const float* side_r = nullptr;
};

class FxUnit {
public:
    virtual ~FxUnit() = default;
    // In place on a stereo block (frames <= the engine's maximum block).
    virtual void process(float* l, float* r, std::size_t frames, const FxContext& ctx) noexcept = 0;
    virtual void reset() noexcept = 0;
    model::FxType type() const { return type_; }
    float sample_rate() const { return rate_; }

protected:
    FxUnit(model::FxType type, float rate) : type_(type), rate_(rate) {}

private:
    model::FxType type_;
    float rate_;
};

// nullptr for FxType::Off.
std::unique_ptr<FxUnit> make_fx_unit(model::FxType type, float sample_rate);

// Fractional-delay ring buffer (power-of-two size, linear interpolation). Sized on creation.
class DelayLine {
public:
    void allocate(std::size_t max_frames) {
        std::size_t n = 1;
        while (n < max_frames + 4) {
            n <<= 1;
        }
        buf_.assign(n, 0.0f);
        mask_ = n - 1;
        pos_ = 0;
    }
    void clear() noexcept { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    void push(float v) noexcept {
        pos_ = (pos_ + 1) & mask_;
        buf_[pos_] = v;
    }
    // Sample written `delay` frames ago (delay >= 0; 0 = the last pushed).
    float tap(std::size_t delay) const noexcept { return buf_[(pos_ - delay) & mask_]; }
    float read(float delay) const noexcept {
        const auto d = static_cast<std::size_t>(delay);
        const float f = delay - static_cast<float>(d);
        const float a = buf_[(pos_ - d) & mask_];
        const float b = buf_[(pos_ - d - 1) & mask_];
        return a + f * (b - a);
    }
    std::size_t capacity() const noexcept { return mask_; }

private:
    std::vector<float> buf_{0.0f};
    std::size_t mask_ = 0;
    std::size_t pos_ = 0;
};

// Linear ramp of a parameter across one block (no zipper noise).
struct Ramp {
    float value = 0.0f;
    bool primed = false;
    // Returns the per-sample increment towards `target` over `frames`.
    float begin(float target, std::size_t frames) noexcept {
        if (!primed) {
            value = target;
            primed = true;
        }
        return frames ? (target - value) / static_cast<float>(frames) : 0.0f;
    }
};

// White noise (LCG), deterministic per unit.
struct Noise {
    std::uint32_t state = 0x1234567u;
    float next() noexcept {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

} // namespace phos::dsp
