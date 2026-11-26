#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace phos {

// Delay-locked loop that maps the engine's frame counter to host time (PLAN.md §6.4), after
// F. Adriaensen, "Using a DLL to filter time" (2005), section 4: a second-order loop with
// b = sqrt(2) * w and c = w^2, w = 2 * pi * B * T, where B is the loop bandwidth and T the period.
// Variable callback sizes are handled by tracking the time per frame instead of per period.
//
// Call update() at every audio callback with the callback's host time, the first frame of the
// block and the block size. The filtered time t0 belongs to that block's first frame.
class TimeDll {
public:
    void reset(std::uint32_t sample_rate, double bandwidth_hz = 1.0) {
        sample_rate_ = sample_rate;
        bandwidth_hz_ = bandwidth_hz;
        updates_ = 0;
    }

    void update(std::int64_t host_ns, std::uint64_t block_frame, std::uint32_t frames) {
        if (frames == 0) {
            return;
        }
        if (updates_ == 0) {
            start(host_ns, block_frame);
        } else {
            const double predicted = t0_ + ns_per_frame_ * static_cast<double>(block_frame - frame0_);
            const double e = static_cast<double>(host_ns) - predicted;
            if (std::fabs(e) > kResyncNs) {
                start(host_ns, block_frame); // xrun, device restart or clock jump
            } else {
                const double period = static_cast<double>(block_frame - frame0_);
                const double w = 2.0 * std::numbers::pi * bandwidth_hz_ * period / sample_rate_;
                t0_ = predicted + std::numbers::sqrt2 * w * e;
                ns_per_frame_ += w * w * e / period;
                frame0_ = block_frame;
            }
        }
        ++updates_;
    }

    // Usable once a few periods have been filtered.
    bool locked() const { return updates_ >= kLockUpdates; }
    std::uint64_t updates() const { return updates_; }

    double frame_to_ns(double frame) const { return t0_ + (frame - static_cast<double>(frame0_)) * ns_per_frame_; }
    double ns_to_frame(double ns) const { return static_cast<double>(frame0_) + (ns - t0_) / ns_per_frame_; }
    double ns_per_frame() const { return ns_per_frame_; }

    static constexpr double kResyncNs = 20e6; // 20 ms
    static constexpr std::uint64_t kLockUpdates = 16;

private:
    void start(std::int64_t host_ns, std::uint64_t block_frame) {
        t0_ = static_cast<double>(host_ns);
        frame0_ = block_frame;
        ns_per_frame_ = 1e9 / sample_rate_;
        updates_ = 0;
    }

    std::uint32_t sample_rate_ = 48000;
    double bandwidth_hz_ = 1.0;
    double t0_ = 0.0;
    std::uint64_t frame0_ = 0;
    double ns_per_frame_ = 1e9 / 48000.0;
    std::uint64_t updates_ = 0;
};

} // namespace phos
