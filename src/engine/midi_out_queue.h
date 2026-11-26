#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// MIDI-out thread side (PLAN.md §7.7): messages with the host time they are due, kept in time
// order (stable for equal times, so a note-off queued before a note-on at the same time stays
// first). Fixed capacity; the thread drains it as messages fall due.
namespace phos::engine {

struct TimedMidi {
    double due_ns = 0.0; // steady_clock nanoseconds
    std::uint8_t slot = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 3> bytes{};
};

class MidiOutQueue {
public:
    explicit MidiOutQueue(std::size_t capacity = 8192) { items_.reserve(capacity); }

    // False when full (the message is dropped and counted).
    bool push(const TimedMidi& m) {
        if (items_.size() == items_.capacity()) {
            ++dropped_;
            return false;
        }
        // Almost always an append: messages arrive nearly in time order.
        auto it = items_.end();
        while (it != items_.begin() && (it - 1)->due_ns > m.due_ns) {
            --it;
        }
        items_.insert(it, m);
        return true;
    }
    bool empty() const { return head_ == items_.size(); }
    double next_due() const { return items_[head_].due_ns; }
    // The earliest message if it is due at `now_ns`.
    bool pop_due(double now_ns, TimedMidi& out) {
        if (empty() || items_[head_].due_ns > now_ns) {
            return false;
        }
        out = items_[head_++];
        if (head_ == items_.size()) {
            items_.clear();
            head_ = 0;
        } else if (head_ > items_.capacity() / 2) {
            items_.erase(items_.begin(), items_.begin() + static_cast<std::ptrdiff_t>(head_));
            head_ = 0;
        }
        return true;
    }
    std::uint64_t dropped() const { return dropped_; }

private:
    std::vector<TimedMidi> items_;
    std::size_t head_ = 0;
    std::uint64_t dropped_ = 0;
};

} // namespace phos::engine
