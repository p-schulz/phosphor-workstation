#pragma once

#include <cstddef>
#include <cstdint>

// MIDI 1.0 byte-stream helpers (MIDI 1.0 Detailed Specification 4.2, "Running Status" and
// "System Real Time Messages"). Realtime-safe: no allocation, fixed state.
namespace phos::midi {

// One channel or system-common message (1..3 bytes).
struct Message {
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint8_t size = 0;

    std::uint8_t type() const { return status & 0xF0; } // 0x80 note off ... 0xE0 pitch bend
    std::uint8_t channel() const { return status & 0x0F; } // 0-based
    bool is_note_on() const { return type() == 0x90 && data2 > 0; }
    bool is_note_off() const { return type() == 0x80 || (type() == 0x90 && data2 == 0); }
};

inline constexpr std::uint8_t kNoteOff = 0x80;
inline constexpr std::uint8_t kNoteOn = 0x90;
inline constexpr std::uint8_t kPolyPressure = 0xA0;
inline constexpr std::uint8_t kControlChange = 0xB0;
inline constexpr std::uint8_t kProgramChange = 0xC0;
inline constexpr std::uint8_t kChannelPressure = 0xD0;
inline constexpr std::uint8_t kPitchBend = 0xE0;

// Data bytes that follow a status byte (channel messages and system common).
constexpr int data_length(std::uint8_t status) {
    switch (status & 0xF0) {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
        return 2;
    case 0xC0:
    case 0xD0:
        return 1;
    default:
        break;
    }
    switch (status) {
    case 0xF1: // MTC quarter frame
    case 0xF3: // song select
        return 1;
    case 0xF2: // song position pointer
        return 2;
    default:
        return 0;
    }
}

// Incremental parser for a raw MIDI byte stream (one per source). Handles running status,
// realtime bytes interleaved anywhere (dropped), and SysEx (skipped up to F7 or the next status).
// Channel messages are reported; system common and realtime are not needed before M8.
class Parser {
public:
    // Feeds bytes; calls `out(const Message&)` for each complete channel message.
    template <typename Fn> void feed(const std::uint8_t* bytes, std::size_t n, Fn&& out) {
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t b = bytes[i];
            if (b >= 0xF8) {
                continue; // realtime: clock, start, stop, active sensing, reset
            }
            if (b & 0x80) {
                in_sysex_ = b == 0xF0;
                if (b >= 0xF0) {
                    running_ = 0; // system common and SysEx cancel running status
                    pending_ = 0;
                    continue;
                }
                running_ = b;
                count_ = 0;
                pending_ = static_cast<std::uint8_t>(data_length(b));
                continue;
            }
            if (in_sysex_ || running_ == 0) {
                continue; // SysEx payload or stray data byte
            }
            data_[count_++] = b;
            if (count_ == pending_) {
                Message m;
                m.status = running_;
                m.data1 = data_[0];
                m.data2 = pending_ > 1 ? data_[1] : 0;
                m.size = static_cast<std::uint8_t>(1 + pending_);
                count_ = 0; // running status: further data bytes reuse the status
                out(m);
            }
        }
    }

    void reset() {
        running_ = 0;
        pending_ = 0;
        count_ = 0;
        in_sysex_ = false;
    }

private:
    std::uint8_t running_ = 0;
    std::uint8_t pending_ = 0;
    std::uint8_t count_ = 0;
    std::uint8_t data_[2] = {0, 0};
    bool in_sysex_ = false;
};

} // namespace phos::midi
