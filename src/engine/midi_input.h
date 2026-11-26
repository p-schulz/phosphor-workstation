#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "core/midi.h"
#include "core/spsc_ring.h"
#include "engine/messages.h"

namespace phos::engine {

// A parsed MIDI message with its host timestamp (steady_clock ns, PLAN.md §6.4).
struct MidiInEvent {
    std::int64_t ns = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint8_t port = 0; // input slot
};

// Per-input SPSC queues from the MIDI-in callbacks to the audio thread. Each open input owns one
// slot; its callback is the only producer of that slot. Capacity 16384 per slot (PLAN.md §6.4
// suggests 4096; raised so a 10 000-note burst delivered in one go fits).
class MidiInputQueues {
public:
    static constexpr int kSlots = kMaxMidiSlots;
    static constexpr std::size_t kCapacity = 16384;

    // MIDI-in callback (producer of `slot`): parse the raw bytes and push. No allocation, no locks.
    void push_bytes(int slot, const std::uint8_t* bytes, std::size_t n, std::int64_t ns) noexcept {
        Slot& s = slots_[static_cast<std::size_t>(slot)];
        s.parser.feed(bytes, n, [&](const midi::Message& m) {
            if (!s.ring.push(MidiInEvent{ns, m.status, m.data1, m.data2, static_cast<std::uint8_t>(slot)})) {
                s.overflow.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Producer side of a slot that is (re)assigned to a new port: forget partial messages.
    void reset_parser(int slot) noexcept { slots_[static_cast<std::size_t>(slot)].parser.reset(); }

    // Audio thread (consumer of all slots).
    bool pop(int slot, MidiInEvent& out) noexcept { return slots_[static_cast<std::size_t>(slot)].ring.pop(out); }

    std::uint64_t overflow(int slot) const noexcept {
        return slots_[static_cast<std::size_t>(slot)].overflow.load(std::memory_order_relaxed);
    }

private:
    struct Slot {
        midi::Parser parser; // producer-owned
        SpscRing<MidiInEvent, kCapacity> ring;
        std::atomic<std::uint64_t> overflow{0};
    };
    std::array<Slot, kSlots> slots_;
};

} // namespace phos::engine
