#pragma once

#include <cstdint>

namespace phos::engine {

// Counts late audio callbacks. CoreAudio and miniaudio do not report under/overruns, so the
// engine tracks when each callback should arrive according to the frames already delivered
// (expected += frames / rate) and counts a callback that arrives more than one period plus a
// tolerance late as an xrun, then re-synchronises. The expected time follows the real callback
// times slowly (1 % per callback) to absorb drift between the audio clock and the host clock.
// The first 0.5 s after start are ignored: devices often deliver bursts while starting.
class XrunDetector {
public:
    void reset(std::uint32_t sample_rate) noexcept {
        sample_rate_ = sample_rate;
        started_ = false;
        warmup_ns_ = 0;
    }

    // Returns true when this callback counts as an xrun.
    bool on_callback(std::int64_t host_ns, std::uint32_t frames) noexcept {
        const double period_ns = 1e9 * frames / sample_rate_;
        if (!started_) {
            started_ = true;
            expected_ns_ = static_cast<double>(host_ns) + period_ns;
            warmup_ns_ = host_ns + kWarmupNs;
            return false;
        }
        const double err = static_cast<double>(host_ns) - expected_ns_;
        bool xrun = false;
        if (host_ns < warmup_ns_) {
            expected_ns_ = static_cast<double>(host_ns);
        } else if (err > period_ns + kToleranceNs) {
            ++xruns_;
            xrun = true;
            expected_ns_ = static_cast<double>(host_ns);
        } else {
            expected_ns_ += err * 0.01;
        }
        expected_ns_ += period_ns;
        return xrun;
    }

    std::uint64_t xruns() const noexcept { return xruns_; }

    static constexpr double kToleranceNs = 1.5e6;     // 1.5 ms on top of one period
    static constexpr std::int64_t kWarmupNs = 500'000'000;

private:
    std::uint32_t sample_rate_ = 48000;
    bool started_ = false;
    double expected_ns_ = 0.0;
    std::int64_t warmup_ns_ = 0;
    std::uint64_t xruns_ = 0;
};

} // namespace phos::engine
