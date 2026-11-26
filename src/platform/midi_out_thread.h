#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "engine/midi_out_queue.h"

// The MIDI-out thread (PLAN.md §7.7): takes timed messages from `source`, sleeps until 1 ms before
// the next one is due, spin-waits the rest and sends it. Elevated priority: macOS time-constraint
// policy (Windows MMCSS "Pro Audio" arrives in M9).
namespace phos::platform {

class MidiOutThread {
public:
    // `source`: next message with its due time (false when none); `send`: to the device.
    using Source = std::function<bool(engine::TimedMidi&)>;
    using Send = std::function<void(const engine::TimedMidi&)>;

    ~MidiOutThread() { stop(); }
    void start(Source source, Send send);
    void stop();
    bool running() const { return thread_.joinable(); }

    // Timing of the messages sent so far: how late they left (ns), worst and mean.
    double worst_late_ns() const { return worst_late_.load(std::memory_order_relaxed); }
    std::uint64_t sent() const { return sent_.load(std::memory_order_relaxed); }

private:
    void run();

    std::thread thread_;
    std::atomic<bool> quit_{false};
    Source source_;
    Send send_;
    std::atomic<double> worst_late_{0.0};
    std::atomic<std::uint64_t> sent_{0};
};

} // namespace phos::platform
