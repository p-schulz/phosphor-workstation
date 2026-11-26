// MIDI side of the engine: draining the input queues, placing events in time (IMMEDIATE /
// STABLE, PLAN.md §6.4), and applying them to the voices. Audio thread only.

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/midi.h"
#include "engine/engine.h"

namespace phos::engine {

std::uint8_t sequencer_status(seq::SeqEventType t) noexcept; // engine_midi_out.cpp

namespace {

constexpr std::uint8_t kCcModWheel = 1;
constexpr std::uint8_t kCcSustain = 64;
constexpr std::uint8_t kCcAllSoundOff = 120;
constexpr std::uint8_t kCcAllNotesOff = 123;

} // namespace

void Engine::drain_midi(std::uint32_t frames, bool host_clock) noexcept {
    MidiInEvent e;
    for (int slot = 0; slot < MidiInputQueues::kSlots; ++slot) {
        // Bounded: at most one queue's capacity per slot per block.
        for (std::size_t i = 0; i < MidiInputQueues::kCapacity && midi_->pop(slot, e); ++i) {
            const std::int8_t filter = current_->slot_channel[static_cast<std::size_t>(slot)];
            if (filter >= 0 && (e.status & 0x0F) != filter) {
                continue;
            }
            schedule_midi(e.status, e.data1, e.data2, e.port, e.ns, frames, host_clock);
        }
    }
}

void Engine::schedule_midi(std::uint8_t status, std::uint8_t d1, std::uint8_t d2, std::uint8_t port, std::int64_t ns,
                           std::uint32_t frames, bool host_clock) noexcept {
    const bool timed = host_clock && dll_.locked() && ns > 0;
    std::uint64_t frame = block_frame_;
    if (timed && current_->placement == Placement::Stable) {
        // Event time on the engine clock plus one block: every event gets the same delay.
        const double f = std::floor(dll_.ns_to_frame(static_cast<double>(ns))) + frames;
        if (f < static_cast<double>(block_frame_)) {
            late_events_.fetch_add(1, std::memory_order_relaxed);
        } else if (f < static_cast<double>(block_frame_ + 4ull * frames)) {
            frame = static_cast<std::uint64_t>(f);
        } // else: timestamp from the future (bad clock) -> immediate
    }
    if (timed) {
        latency_.add(static_cast<std::int64_t>(dll_.frame_to_ns(static_cast<double>(frame))) - ns);
    }
    // Recording uses when the key was pressed (its timestamp), not when it is rendered.
    const std::uint8_t type = status & 0xF0;
    if (seq_.recording() && (type == midi::kNoteOn || type == midi::kNoteOff)) {
        const std::uint64_t pressed = timed ? static_cast<std::uint64_t>(std::max(0.0, std::floor(dll_.ns_to_frame(static_cast<double>(ns)))))
                                            : block_frame_;
        record(type == midi::kNoteOn && d2 > 0, d1, d2, pressed);
    }

    const Scheduled e{frame, status, d1, d2, port, 0};
    if (frame == block_frame_ && next_due() > block_frame_) {
        apply_midi(e); // due now, nothing earlier pending
        return;
    }
    enqueue(e);
}

void Engine::record(bool on, std::uint8_t note, std::uint8_t velocity, std::uint64_t frame) noexcept {
    const seq::CompiledSong& s = current_->song;
    const auto offset = static_cast<std::int64_t>(s.record_offset_ms) * sample_rate_ / 1000;
    const auto at = static_cast<std::uint64_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(frame) - offset));
    rec_events_.push(RecEvent{on, note, velocity, static_cast<std::uint8_t>(s.current_track),
                              static_cast<std::int16_t>(seq_.pattern(s)), seq_.tick_at(s, at, sample_rate_)});
}

void Engine::schedule_sequencer(const seq::SeqEvent& ev) noexcept {
    Scheduled e;
    e.frame = ev.frame;
    e.track = ev.track;
    e.data1 = ev.note;
    e.data2 = ev.velocity;
    switch (ev.type) {
    case seq::SeqEventType::NoteOn:
        e.status = midi::kNoteOn;
        e.port = kSequencerPort;
        break;
    case seq::SeqEventType::NoteOff:
        e.status = midi::kNoteOff;
        e.port = kSequencerPort;
        break;
    case seq::SeqEventType::Click:
    case seq::SeqEventType::AccentClick:
        e.status = midi::kNoteOn;
        e.port = kMetronomePort;
        e.data1 = ev.type == seq::SeqEventType::AccentClick ? 1 : 0;
        break;
    case seq::SeqEventType::Clock:
    case seq::SeqEventType::Start:
    case seq::SeqEventType::Continue:
    case seq::SeqEventType::PatternStart:
        if (!current_->midi_out) {
            return; // nothing to send: keep the scheduler free for notes
        }
        e.status = sequencer_status(ev.type);
        e.port = kSequencerPort;
        break;
    case seq::SeqEventType::Cc:
        e.status = sequencer_status(ev.type);
        e.port = kSequencerPort;
        break;
    }
    enqueue(e);
}

void Engine::flush_sequencer_events() noexcept {
    std::size_t w = sched_head_;
    for (std::size_t r = sched_head_; r < sched_size_; ++r) {
        if (schedule_[r].port != kSequencerPort && schedule_[r].port != kMetronomePort) {
            schedule_[w++] = schedule_[r];
        }
    }
    sched_size_ = w;
}

void Engine::enqueue(const Scheduled& e) noexcept {
    const std::uint64_t frame = e.frame;
    if (sched_head_ > 0 && sched_head_ == sched_size_) {
        sched_head_ = sched_size_ = 0; // empty: rewind
    }
    if (sched_size_ == schedule_.size()) {
        if (sched_head_ == 0) {
            // Full: play it now rather than lose it.
            sched_overflow_.fetch_add(1, std::memory_order_relaxed);
            apply_midi(e);
            return;
        }
        // Compact the consumed head.
        std::move(schedule_.begin() + static_cast<std::ptrdiff_t>(sched_head_),
                  schedule_.begin() + static_cast<std::ptrdiff_t>(sched_size_), schedule_.begin());
        sched_size_ -= sched_head_;
        sched_head_ = 0;
    }
    // Sorted insert after events with the same frame, so arrival order is kept. Events arrive
    // almost in order, so this is usually an append.
    std::size_t i = sched_size_;
    while (i > sched_head_ && schedule_[i - 1].frame > frame) {
        schedule_[i] = schedule_[i - 1];
        --i;
    }
    schedule_[i] = e;
    ++sched_size_;
}

std::uint64_t Engine::next_due() const noexcept {
    return sched_head_ < sched_size_ ? schedule_[sched_head_].frame : std::numeric_limits<std::uint64_t>::max();
}

void Engine::apply_due(std::uint64_t frame) noexcept {
    while (sched_head_ < sched_size_ && schedule_[sched_head_].frame <= frame) {
        apply_midi(schedule_[sched_head_]);
        ++sched_head_;
    }
}

float Engine::velocity_gain(std::uint8_t velocity) const noexcept {
    const float v = static_cast<float>(velocity) / 127.0f;
    switch (current_->velocity_curve) {
    case VelocityCurve::Soft:
        return std::sqrt(v);
    case VelocityCurve::Hard:
        return v * v;
    case VelocityCurve::Fixed:
        return 100.0f / 127.0f;
    case VelocityCurve::Linear:
        break;
    }
    return v;
}

void Engine::apply_midi(const Scheduled& e) noexcept {
    const std::uint8_t type = e.status & 0xF0;
    if (e.port == kMetronomePort) {
        voices_.note_on(TestWave::Click, e.data1, static_cast<float>(e.data2) / 127.0f, kDirectBus);
        return;
    }
    if (e.port == kSequencerPort) {
        if (sequencer_system(e)) {
            return;
        }
        const int track = e.track;
        if (current_->song.tracks[static_cast<std::size_t>(track)].type == model::TrackType::Loops) {
            // A LOOPS lane: the note picks the slot, its length is how long the slot plays.
            const int loop = ((e.data1 - model::kLoopBaseNote) % model::kLoopSlots + model::kLoopSlots) % model::kLoopSlots;
            if (type == midi::kNoteOn && e.data2 > 0) {
                loops_.launch(loop, e.frame);
            } else {
                loops_.stop(loop, e.frame);
            }
            return;
        }
        const auto& tb = current_->song.tracks[static_cast<std::size_t>(track)];
        if (tb.dest != model::MidiDest::Internal) {
            const bool on = type == midi::kNoteOn && e.data2 > 0;
            out(e.frame, current_->track_out_slot[static_cast<std::size_t>(track)],
                static_cast<std::uint8_t>((on ? midi::kNoteOn : midi::kNoteOff) | tb.out_channel), e.data1,
                on ? e.data2 : std::uint8_t{0}, 3);
            if (tb.dest == model::MidiDest::External) {
                return;
            }
        }
        const int slot = tb.instrument;
        if (type == midi::kNoteOn && e.data2 > 0) {
            const SamplerBlock& inst = current_->instruments[static_cast<std::size_t>(slot)];
            if (inst.playable()) {
                voices_.note_on_instrument(slot, current_->instruments, e.data1, velocity_gain(e.data2), track);
            } else {
                voices_.note_on(TestWave::Sine, e.data1, velocity_gain(e.data2), track);
            }
        } else {
            voices_.note_off(e.data1, track, false);
        }
        return;
    }
    midi_events_.fetch_add(1, std::memory_order_relaxed);
    const int live_track = current_->song.current_track;
    if (thru(e)) { // EXTERNAL track: forwarded only (the held-note display and CC monitor still see it)
        const std::uint8_t t = e.status & 0xF0;
        if (t == midi::kNoteOn || t == midi::kNoteOff) {
            const bool on = t == midi::kNoteOn && e.data2 > 0;
            note_ons_.fetch_add(on ? 1 : 0, std::memory_order_relaxed);
            const std::uint64_t bit = std::uint64_t{1} << (e.data1 & 63);
            auto& word = held_local_[static_cast<std::size_t>((e.data1 & 0x7F) >> 6)];
            word = on ? (word | bit) : (word & ~bit);
        } else if (t == midi::kControlChange) {
            cc_events_.push(CcEvent{e.port, static_cast<std::uint8_t>(e.status & 0x0F), e.data1, e.data2});
        }
        return;
    }
    const std::uint8_t channel = e.status & 0x0F;
    auto set_held = [&](int note, bool on) {
        const std::uint64_t bit = std::uint64_t{1} << (note & 63);
        auto& word = held_local_[static_cast<std::size_t>(note >> 6)];
        word = on ? (word | bit) : (word & ~bit);
    };

    const bool loop_track = current_->song.tracks[static_cast<std::size_t>(live_track)].type == model::TrackType::Loops;
    if (loop_track && (type == midi::kNoteOn || type == midi::kNoteOff)) {
        // Playing on a LOOPS track: each key toggles its slot, quantised like Page L.
        if (type == midi::kNoteOn && e.data2 > 0) {
            note_ons_.fetch_add(1, std::memory_order_relaxed);
            const int loop = ((e.data1 - model::kLoopBaseNote) % model::kLoopSlots + model::kLoopSlots) % model::kLoopSlots;
            loop_command(loop, 2, e.frame);
        }
        return;
    }
    if (type == midi::kNoteOn && e.data2 > 0) {
        note_ons_.fetch_add(1, std::memory_order_relaxed);
        const int slot = current_->current_instrument;
        const SamplerBlock& inst = current_->instruments[static_cast<std::size_t>(slot)];
        if (inst.playable()) {
            voices_.note_on_instrument(slot, current_->instruments, e.data1, velocity_gain(e.data2), live_track);
        } else {
            voices_.note_on(TestWave::Sine, e.data1, velocity_gain(e.data2), live_track); // empty: test tone
        }
        set_held(e.data1 & 0x7F, true);
        return;
    }
    if (type == midi::kNoteOff || type == midi::kNoteOn) {
        voices_.note_off(e.data1, live_track);
        set_held(e.data1 & 0x7F, false);
        return;
    }
    if (type == midi::kPitchBend) {
        const int value = (e.data2 << 7 | e.data1) - 8192; // 14 bit, centre 8192
        const double semitones = current_->bend_range_semitones * static_cast<double>(value) / 8192.0;
        voices_.set_pitch_ratio(std::exp2(semitones / 12.0));
        return;
    }
    if (type != midi::kControlChange) {
        return; // channel pressure and program change: no targets before M3
    }
    cc_events_.push(CcEvent{e.port, channel, e.data1, e.data2}); // dropped when the UI lags
    switch (e.data1) {
    case kCcModWheel:
        voices_.set_vibrato(static_cast<float>(e.data2) / 127.0f);
        break;
    case kCcSustain:
        voices_.set_sustain(e.data2 >= 64);
        break;
    case kCcAllSoundOff:
    case kCcAllNotesOff:
        voices_.release_all();
        held_local_ = {};
        break;
    default:
        break;
    }
    for (int i = 0; i < current_->cc_binding_count; ++i) {
        const CcBinding& b = current_->cc_bindings[static_cast<std::size_t>(i)];
        if (b.cc != e.data1 || (b.channel != 0xFF && b.channel != channel)) {
            continue;
        }
        if (b.param == EngineParam::MasterGainDb) {
            // 0..127 -> -60..+6 dB
            const float db = -60.0f + 66.0f * static_cast<float>(e.data2) / 127.0f;
            master_gain_.set_target(dsp::db_to_gain(db));
        }
    }
}

} // namespace phos::engine
