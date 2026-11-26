// MIDI out and automation on the audio thread (PLAN.md §7.7): sequencer notes and CC lanes for
// EXTERNAL / BOTH tracks, program changes at pattern starts, MIDI clock with start / continue
// (song position pointer) / stop, THRU of live input, all-notes-off on STOP and PANIC. Messages
// are queued with their engine frame; the MIDI-out thread sends them on time.

#include <cmath>
#include <limits>

#include "core/midi.h"
#include "engine/engine.h"

namespace phos::engine {

namespace {

constexpr std::uint8_t kClock = 0xF8;
constexpr std::uint8_t kStart = 0xFA;
constexpr std::uint8_t kContinue = 0xFB;
constexpr std::uint8_t kSongPosition = 0xF2;
constexpr std::uint8_t kPatternStart = 0xF9; // internal marker (undefined in MIDI 1.0)
constexpr std::uint8_t kAllSoundOff = 120;
constexpr std::uint8_t kAllNotesOff = 123;

} // namespace

std::uint8_t sequencer_status(seq::SeqEventType t) noexcept {
    switch (t) {
    case seq::SeqEventType::Cc:
        return midi::kControlChange;
    case seq::SeqEventType::Clock:
        return kClock;
    case seq::SeqEventType::PatternStart:
        return kPatternStart;
    case seq::SeqEventType::Start:
        return kStart;
    case seq::SeqEventType::Continue:
        return kContinue;
    default:
        return 0;
    }
}

void Engine::out(std::uint64_t frame, int slot, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2,
                 std::uint8_t size) noexcept {
    if (!current_->midi_out || slot < 0 || slot >= kMaxMidiOutSlots) {
        return;
    }
    MidiOutEvent e;
    e.frame = frame;
    e.slot = static_cast<std::uint8_t>(slot);
    e.size = size;
    e.bytes = {b0, b1, b2};
    if (!midi_out_.push(e)) {
        midi_out_lost_.fetch_add(1, std::memory_order_relaxed);
    }
}

void Engine::out_transport(std::uint64_t frame, std::uint8_t status) noexcept {
    for (int s = 0; s < kMaxMidiOutSlots; ++s) {
        if (current_->clock_slots & (1u << s)) {
            out(frame, s, status, 0, 0, 1);
        }
    }
}

void Engine::out_notes_off(std::uint64_t frame, bool panic) noexcept {
    // STOP: the channels external tracks use. PANIC: every channel of every open output.
    std::array<std::uint16_t, kMaxMidiOutSlots> channels{};
    if (panic) {
        for (int s = 0; s < kMaxMidiOutSlots; ++s) {
            channels[static_cast<std::size_t>(s)] = current_->open_slots & (1u << s) ? 0xFFFF : 0;
        }
    } else {
        for (int t = 0; t < kTrackCount; ++t) {
            const auto& tb = current_->song.tracks[static_cast<std::size_t>(t)];
            const int slot = current_->track_out_slot[static_cast<std::size_t>(t)];
            if (tb.dest != model::MidiDest::Internal && slot >= 0) {
                channels[static_cast<std::size_t>(slot)] |= static_cast<std::uint16_t>(1u << tb.out_channel);
            }
        }
    }
    for (int s = 0; s < kMaxMidiOutSlots; ++s) {
        for (int ch = 0; ch < 16; ++ch) {
            if (channels[static_cast<std::size_t>(s)] & (1u << ch)) {
                const auto st = static_cast<std::uint8_t>(midi::kControlChange | ch);
                if (panic) {
                    out(frame, s, st, kAllSoundOff, 0, 3);
                }
                out(frame, s, st, kAllNotesOff, 0, 3);
            }
        }
    }
}

void Engine::reset_automation() noexcept {
    auto_level_.fill(1.0f);
    auto_volume_.fill(1.0f);
    auto_expr_.fill(1.0f);
    auto_pan_.fill(std::numeric_limits<float>::quiet_NaN());
}

void Engine::automation(int track, std::uint8_t cc, std::uint8_t value) noexcept {
    // Inside PHOSPHOR: volume and expression scale the track (squared: about a dB curve), pan
    // replaces the track pan, the mod wheel sets the vibrato. CC 7 and CC 11 are kept separately.
    auto& volume = auto_volume_;
    auto& expression = auto_expr_;
    const auto t = static_cast<std::size_t>(track);
    const float v = static_cast<float>(value) / 127.0f;
    switch (cc) {
    case model::kCcVolume:
        volume[t] = v * v;
        break;
    case model::kCcExpression:
        expression[t] = v * v;
        break;
    case model::kCcPan:
        auto_pan_[t] = value >= 64 ? static_cast<float>(value - 64) / 63.0f : static_cast<float>(value - 64) / 64.0f;
        return;
    case model::kCcModWheel:
        voices_.set_vibrato(v);
        return;
    default:
        return;
    }
    auto_level_[t] = volume[t] * expression[t];
}

bool Engine::sequencer_system(const Scheduled& e) noexcept {
    const auto& song = current_->song;
    switch (e.status) {
    case kClock:
    case kStart:
        out_transport(e.frame, e.status);
        return true;
    case kContinue:
        for (int s = 0; s < kMaxMidiOutSlots; ++s) {
            if (current_->clock_slots & (1u << s)) {
                out(e.frame, s, kSongPosition, e.data1, e.data2, 3);
                out(e.frame, s, kContinue, 0, 0, 1);
            }
        }
        return true;
    case kPatternStart:
        for (int t = 0; t < kTrackCount; ++t) {
            const auto& tb = song.tracks[static_cast<std::size_t>(t)];
            if (tb.program >= 0 && tb.dest != model::MidiDest::Internal) {
                out(e.frame, current_->track_out_slot[static_cast<std::size_t>(t)],
                    static_cast<std::uint8_t>(midi::kProgramChange | tb.out_channel), static_cast<std::uint8_t>(tb.program), 0, 2);
            }
        }
        return true;
    case midi::kControlChange: {
        const auto& tb = song.tracks[e.track];
        if (tb.dest != model::MidiDest::Internal) {
            out(e.frame, current_->track_out_slot[e.track], static_cast<std::uint8_t>(midi::kControlChange | tb.out_channel),
                e.data1, e.data2, 3);
        }
        if (tb.dest != model::MidiDest::External) {
            automation(e.track, e.data1, e.data2);
        }
        return true;
    }
    default:
        return false;
    }
}

bool Engine::thru(const Scheduled& e) noexcept {
    const auto& song = current_->song;
    const auto t = static_cast<std::size_t>(song.current_track);
    const auto& tb = song.tracks[t];
    if (tb.dest == model::MidiDest::Internal || tb.type == model::TrackType::Loops) {
        return false;
    }
    const std::uint8_t type = e.status & 0xF0;
    if (current_->thru && type >= midi::kNoteOff && type <= 0xE0) {
        const std::uint8_t size = type == midi::kProgramChange || type == 0xD0 ? 2 : 3;
        out(e.frame, current_->track_out_slot[t], static_cast<std::uint8_t>(type | tb.out_channel), e.data1, e.data2, size);
    }
    return tb.dest == model::MidiDest::External;
}

void Engine::publish_clock() noexcept {
    // Seqlock writer (audio thread only): odd while the fields change.
    const std::uint32_t s = clock_seq_.load(std::memory_order_relaxed);
    clock_seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    clock_t0_.store(dll_.frame_to_ns(static_cast<double>(block_frame_)), std::memory_order_relaxed);
    clock_frame0_.store(block_frame_, std::memory_order_relaxed);
    clock_npf_.store(dll_.ns_per_frame(), std::memory_order_relaxed);
    clock_locked_.store(dll_.locked(), std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    clock_seq_.store(s + 2, std::memory_order_release);
}

bool Engine::frame_to_ns(std::uint64_t frame, double& ns) const {
    for (int tries = 0; tries < 16; ++tries) {
        const std::uint32_t s0 = clock_seq_.load(std::memory_order_acquire);
        if (s0 & 1u) {
            continue;
        }
        const double t0 = clock_t0_.load(std::memory_order_relaxed);
        const std::uint64_t f0 = clock_frame0_.load(std::memory_order_relaxed);
        const double npf = clock_npf_.load(std::memory_order_relaxed);
        const bool locked = clock_locked_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (clock_seq_.load(std::memory_order_relaxed) == s0) {
            ns = t0 + (static_cast<double>(frame) - static_cast<double>(f0)) * npf;
            return locked;
        }
    }
    return false;
}

} // namespace phos::engine
