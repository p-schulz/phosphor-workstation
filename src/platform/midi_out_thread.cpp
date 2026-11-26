#include "platform/midi_out_thread.h"

#include <algorithm>
#include <chrono>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#endif

namespace phos::platform {

namespace {

double now_ns() {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void raise_priority() {
#if defined(__APPLE__)
    // Time-constraint policy: period 1 ms, up to 0.2 ms of work, must finish within 1 ms.
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    const auto abs = [&](double ns) { return static_cast<std::uint32_t>(ns * tb.denom / tb.numer); };
    thread_time_constraint_policy_data_t p;
    p.period = abs(1e6);
    p.computation = abs(2e5);
    p.constraint = abs(1e6);
    p.preemptible = 1;
    thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, reinterpret_cast<thread_policy_t>(&p),
                      THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
}

constexpr double kSpinNs = 1e6;   // spin-wait the last millisecond
constexpr double kPollNs = 2e6;   // look for new messages at least every 2 ms

} // namespace

void MidiOutThread::start(Source source, Send send) {
    stop();
    source_ = std::move(source);
    send_ = std::move(send);
    quit_.store(false);
    thread_ = std::thread([this] { run(); });
}

void MidiOutThread::stop() {
    quit_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void MidiOutThread::run() {
    raise_priority();
    engine::MidiOutQueue queue;
    engine::TimedMidi m;
    while (!quit_.load(std::memory_order_relaxed)) {
        while (source_(m)) {
            queue.push(m);
        }
        double now = now_ns();
        while (queue.pop_due(now, m)) {
            send_(m);
            const double late = now_ns() - m.due_ns;
            if (late > worst_late_.load(std::memory_order_relaxed)) {
                worst_late_.store(late, std::memory_order_relaxed);
            }
            sent_.fetch_add(1, std::memory_order_relaxed);
            now = now_ns();
        }
        const double wait = queue.empty() ? kPollNs : std::min(kPollNs, queue.next_due() - now);
        if (wait > kSpinNs) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<std::int64_t>(wait - kSpinNs)));
        } else if (!queue.empty()) {
            const double due = queue.next_due();
            while (now_ns() < due && !quit_.load(std::memory_order_relaxed)) {
                // spin: the message is due within a millisecond
            }
        }
    }
}

} // namespace phos::platform
