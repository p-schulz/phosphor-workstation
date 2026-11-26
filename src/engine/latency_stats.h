#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace phos::engine {

// MIDI scheduling delay: host timestamp of the event -> host time of the frame it is rendered at
// (both through the engine's DLL). One writer (audio thread), any number of readers.
class LatencyStats {
public:
    static constexpr int kBuckets = 24;
    static constexpr std::int64_t kBucketNs = 250'000; // 0.25 ms; the last bucket collects the rest

    struct View {
        std::uint64_t count = 0;
        double avg_ms = 0.0;
        double min_ms = 0.0;
        double max_ms = 0.0;
        std::array<std::uint64_t, kBuckets> histogram{};
        double jitter_ms() const { return count ? max_ms - min_ms : 0.0; }
    };

    // Audio thread.
    void add(std::int64_t delay_ns) noexcept {
        if (reset_.exchange(false, std::memory_order_acquire)) {
            clear();
        }
        count_.fetch_add(1, std::memory_order_relaxed);
        sum_ns_.fetch_add(delay_ns, std::memory_order_relaxed);
        if (delay_ns < min_ns_.load(std::memory_order_relaxed)) {
            min_ns_.store(delay_ns, std::memory_order_relaxed);
        }
        if (delay_ns > max_ns_.load(std::memory_order_relaxed)) {
            max_ns_.store(delay_ns, std::memory_order_relaxed);
        }
        const auto b = std::clamp<std::int64_t>(delay_ns / kBucketNs, 0, kBuckets - 1);
        buckets_[static_cast<std::size_t>(b)].fetch_add(1, std::memory_order_relaxed);
    }
    // Audio thread, once per block: applies a pending reset even when no events arrive.
    void poll_reset() noexcept {
        if (reset_.exchange(false, std::memory_order_acquire)) {
            clear();
        }
    }

    // UI thread.
    void request_reset() noexcept { reset_.store(true, std::memory_order_release); }
    View view() const noexcept {
        View v;
        v.count = count_.load(std::memory_order_relaxed);
        if (v.count == 0) {
            return v;
        }
        v.avg_ms = static_cast<double>(sum_ns_.load(std::memory_order_relaxed)) / static_cast<double>(v.count) / 1e6;
        v.min_ms = static_cast<double>(min_ns_.load(std::memory_order_relaxed)) / 1e6;
        v.max_ms = static_cast<double>(max_ns_.load(std::memory_order_relaxed)) / 1e6;
        for (std::size_t i = 0; i < v.histogram.size(); ++i) {
            v.histogram[i] = buckets_[i].load(std::memory_order_relaxed);
        }
        return v;
    }

private:
    void clear() noexcept {
        count_.store(0, std::memory_order_relaxed);
        sum_ns_.store(0, std::memory_order_relaxed);
        min_ns_.store(std::numeric_limits<std::int64_t>::max(), std::memory_order_relaxed);
        max_ns_.store(std::numeric_limits<std::int64_t>::min(), std::memory_order_relaxed);
        for (auto& b : buckets_) {
            b.store(0, std::memory_order_relaxed);
        }
    }

    std::atomic<bool> reset_{false};
    std::atomic<std::uint64_t> count_{0};
    std::atomic<std::int64_t> sum_ns_{0};
    std::atomic<std::int64_t> min_ns_{std::numeric_limits<std::int64_t>::max()};
    std::atomic<std::int64_t> max_ns_{std::numeric_limits<std::int64_t>::min()};
    std::array<std::atomic<std::uint64_t>, kBuckets> buckets_{};
};

} // namespace phos::engine
