#include "engine/engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#include "core/denormals.h"
#include "core/rt_guard.h"
#include "engine/atomic_max.h"
#include "engine/wavetables.h"

namespace phos::engine {

namespace {

constexpr int kMaxCommandsPerBlock = 256; // bounded drain (no unbounded loops on the audio thread)
constexpr float kParamSmoothingMs = 5.0f;

std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

Engine::Engine()
    : midi_(std::make_unique<MidiInputQueues>()), left_(kMaxBlock), right_(kMaxBlock),
      bus_audio_(static_cast<std::size_t>(kBusCount) * 2 * kMaxBlock), return_audio_(4 * kMaxBlock),
      schedule_(kScheduleCapacity) {
    for (std::size_t i = 0; i < ret_.size(); ++i) {
        ret_[i] = return_audio_.data() + i * kMaxBlock;
    }
    for (int b = 0; b < kBusCount; ++b) {
        bus_l_[static_cast<std::size_t>(b)] = bus_audio_.data() + static_cast<std::size_t>(b) * 2 * kMaxBlock;
        bus_r_[static_cast<std::size_t>(b)] = bus_l_[static_cast<std::size_t>(b)] + kMaxBlock;
    }
    current_ = std::make_unique<EngineSnapshot>().release();
    master_gain_.reset(1.0f);
    prepare(48000, 2);
}

Engine::~Engine() {
    // The device is closed before the engine is destroyed, so this thread owns every pointer.
    delete current_;
    for (int i = 0; i < retired_count_; ++i) {
        delete retired_[static_cast<std::size_t>(i)];
    }
    EngineSnapshot* p = nullptr;
    while (incoming_.pop(p)) {
        delete p;
    }
    collect_garbage();
}

bool Engine::send(const EngineCommand& command) {
    if (commands_.push(command)) {
        return true;
    }
    ++dropped_commands_;
    return false;
}

bool Engine::publish(std::unique_ptr<EngineSnapshot> snapshot) {
    EngineSnapshot* raw = snapshot.get();
    if (!incoming_.push(raw)) {
        return false; // unique_ptr frees it
    }
    snapshot.release();
    return true;
}

int Engine::collect_garbage() {
    int n = 0;
    EngineSnapshot* p = nullptr;
    while (released_.pop(p)) {
        delete p;
        ++n;
    }
    return n;
}

EngineMeters Engine::read_meters() {
    EngineMeters m;
    m.peak_l = peak_l_.exchange(0.0f, std::memory_order_relaxed);
    m.peak_r = peak_r_.exchange(0.0f, std::memory_order_relaxed);
    m.gain_reduction_db = dsp::gain_to_db(min_gain_.exchange(1.0f, std::memory_order_relaxed));
    m.cpu = cpu_.load(std::memory_order_relaxed);
    m.cpu_peak = cpu_peak_.load(std::memory_order_relaxed);
    m.voices = voice_count_.load(std::memory_order_relaxed);
    m.callbacks = callbacks_.load(std::memory_order_relaxed);
    m.frames = frames_.load(std::memory_order_relaxed);
    m.xruns = xrun_count_.load(std::memory_order_relaxed);
    m.overloads = overloads_.load(std::memory_order_relaxed);
    m.dropped_commands = dropped_commands_;
    m.sample_rate = rate_.load(std::memory_order_relaxed);
    m.last_block = last_block_.load(std::memory_order_relaxed);
    return m;
}

MidiMeters Engine::read_midi_meters() const {
    MidiMeters m;
    m.events = midi_events_.load(std::memory_order_relaxed);
    m.note_ons = note_ons_.load(std::memory_order_relaxed);
    m.late = late_events_.load(std::memory_order_relaxed);
    m.schedule_overflow = sched_overflow_.load(std::memory_order_relaxed);
    for (int s = 0; s < MidiInputQueues::kSlots; ++s) {
        m.queue_overflow += midi_->overflow(s);
    }
    m.held = {held_[0].load(std::memory_order_relaxed), held_[1].load(std::memory_order_relaxed)};
    return m;
}

void Engine::reset_counters() {
    cpu_peak_.store(0.0f, std::memory_order_relaxed);
    xrun_count_.store(0, std::memory_order_relaxed);
    overloads_.store(0, std::memory_order_relaxed);
    dropped_commands_ = 0;
}

void Engine::prepare(std::uint32_t sample_rate, std::uint32_t /*channels*/) {
    sample_rate_ = sample_rate;
    rate_.store(sample_rate, std::memory_order_relaxed);
    const auto sr = static_cast<float>(sample_rate);
    voices_.prepare(sr);
    loops_.prepare(sr);
    reset_automation();
    master_gain_.prepare(sr, kParamSmoothingMs);
    master_gain_.reset(master_gain_.target()); // keep the user's level across device changes
    limiter_.prepare(sr);
    snes_bus_.prepare(sample_rate);
    for (int t = 0; t < kTrackCount; ++t) {
        auto& g = track_gain_[static_cast<std::size_t>(t)];
        g.prepare(sr, kParamSmoothingMs);
        g.reset(current_->song.tracks[static_cast<std::size_t>(t)].gain);
    }
    seq_.stop(current_->song, block_frame_, sample_rate);
    apply_snapshot(*current_);
    xruns_.reset(sample_rate);
    dll_.reset(sample_rate);
    sched_head_ = 0;
    sched_size_ = 0;
    held_local_ = {};
    held_[0].store(0, std::memory_order_relaxed);
    held_[1].store(0, std::memory_order_relaxed);
    cpu_smoothed_ = 0.0f;
}

void Engine::apply_snapshot(const EngineSnapshot& s) noexcept {
    limiter_.set_ceiling_db(s.limiter_ceiling_db);
    limiter_.set_release_ms(s.limiter_release_ms);
    voices_.set_envelope(s.voice_attack_ms, s.voice_release_ms);
    voices_.set_limit(s.voice_limit);
    voices_.set_strict(s.song.strict_cmi, s.song.strict_gb, s.song.strict_snes);
}

void Engine::handle(const EngineCommand& c) noexcept {
    switch (c.type) {
    case EngineCommand::Type::NoteOn:
        voices_.note_on(c.wave, c.note, c.value);
        break;
    case EngineCommand::Type::NoteOff:
        voices_.note_off(c.note);
        break;
    case EngineCommand::Type::AllNotesOff:
        voices_.release_all();
        break;
    case EngineCommand::Type::Param:
        if (c.param == EngineParam::MasterGainDb) {
            master_gain_.set_target(dsp::db_to_gain(std::clamp(c.value, -96.0f, 6.0f)));
        }
        break;
    case EngineCommand::Type::SamplerNoteOn:
        if (c.slot >= 0 && c.slot < kInstrumentSlots) {
            voices_.note_on_instrument(c.slot, current_->instruments, c.note, c.value, kDirectBus);
        }
        break;
    case EngineCommand::Type::Transport:
        transport(static_cast<TransportAction>(c.note));
        break;
    case EngineCommand::Type::Panic: {
        // Drop live events still waiting in the scheduler (the sequencer keeps its own).
        std::size_t w = sched_head_;
        for (std::size_t r = sched_head_; r < sched_size_; ++r) {
            if (schedule_[r].port == kSequencerPort || schedule_[r].port == kMetronomePort) {
                schedule_[w++] = schedule_[r];
            }
        }
        sched_size_ = w;
        voices_.set_sustain(false);
        voices_.kill_all();
        voices_.set_pitch_ratio(1.0);
        voices_.set_vibrato(0.0f);
        loops_.kill_all();
        held_local_ = {};
        reset_automation();
        out_notes_off(block_frame_, true);
        break;
    }
    case EngineCommand::Type::Loop:
        loop_command(c.slot, c.note, block_frame_);
        break;
    case EngineCommand::Type::SamplerRelease:
        voices_.release_slot(c.slot);
        break;
    case EngineCommand::Type::Midi:
        break; // scheduled in process()
    }
}

void Engine::process(float* out, std::uint32_t frames, std::uint32_t channels, std::int64_t host_ns) noexcept {
    rt::Scope realtime;
    ScopedFlushDenormals ftz;
    const bool host_clock = host_ns != 0;

    swap_snapshot();
    if (song_changed_) {
        song_changed_ = false;
        if (seq_.playing()) {
            seq_.resync(current_->song, block_frame_, sample_rate_);
        }
    }
    latency_.poll_reset();
    if (host_clock) {
        dll_.update(host_ns, block_frame_, frames);
        if (xruns_.on_callback(host_ns, frames)) {
            xrun_count_.fetch_add(1, std::memory_order_relaxed);
        }
        publish_clock();
    }

    EngineCommand cmd;
    for (int i = 0; i < kMaxCommandsPerBlock && commands_.pop(cmd); ++i) {
        if (cmd.type == EngineCommand::Type::Midi) {
            schedule_midi(cmd.midi[0], cmd.midi[1], cmd.midi[2], kKeyjazzPort, cmd.ns, frames, host_clock);
        } else {
            handle(cmd);
        }
    }
    drain_midi(frames, host_clock);
    seq_.advance(current_->song, block_frame_, frames, sample_rate_,
                 [this](const seq::SeqEvent& ev) { schedule_sequencer(ev); });
    publish_transport();

    // Render, splitting the block at every scheduled MIDI event (sample-accurate placement).
    for (std::uint32_t pos = 0; pos < frames;) {
        const std::uint64_t now = block_frame_ + pos;
        apply_due(now);
        const std::uint64_t due = next_due();
        std::uint32_t n = std::min(frames - pos, kMaxBlock);
        if (due > now && due - now < n) {
            n = static_cast<std::uint32_t>(due - now);
        }
        render_block(out + static_cast<std::size_t>(pos) * channels, n, channels, now);
        pos += n;
    }
    block_frame_ += frames;

    held_[0].store(held_local_[0], std::memory_order_relaxed);
    held_[1].store(held_local_[1], std::memory_order_relaxed);
    voice_count_.store(voices_.active(), std::memory_order_relaxed);
    loops_.publish(loop_state_, loop_phase_);
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    frames_.fetch_add(frames, std::memory_order_relaxed);
    last_block_.store(frames, std::memory_order_relaxed);
    if (host_clock && frames > 0) {
        const double block_ns = 1e9 * frames / sample_rate_;
        const auto load = static_cast<float>(static_cast<double>(now_ns() - host_ns) / block_ns);
        cpu_smoothed_ += (load - cpu_smoothed_) * 0.05f;
        cpu_.store(cpu_smoothed_, std::memory_order_relaxed);
        store_max(cpu_peak_, load);
        if (load > 1.0f) {
            overloads_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool Engine::voices_covered_by(const EngineSnapshot& s) const noexcept {
    return voices_.all_resources([&](const void* p) {
        for (const auto& b : s.instruments) {
            if (b.sample == p || b.tables == p || b.brr == p) {
                return true;
            }
        }
        return false;
    });
}

void Engine::swap_snapshot() noexcept {
    // Hand back retired snapshots once every playing sample is also held by the current one.
    if (retired_count_ > 0 && voices_covered_by(*current_)) {
        while (retired_count_ > 0 && released_.push(retired_[static_cast<std::size_t>(retired_count_ - 1)])) {
            --retired_count_;
        }
    }
    if (released_.free_space() == 0 || retired_count_ == kRetired) {
        return; // try again next block
    }
    EngineSnapshot* next = nullptr;
    if (!incoming_.pop(next)) {
        return;
    }
    EngineSnapshot* old = current_;
    current_ = next;
    apply_snapshot(*current_);
    song_changed_ = true;
    if (voices_.active() == 0 && !loops_.any_active()) {
        // Nothing sounds: faders jump to their new levels instead of gliding (a mute set before
        // PLAY must not let the first 5 ms through; there is nothing that could click).
        for (int t = 0; t < kTrackCount; ++t) {
            track_gain_[static_cast<std::size_t>(t)].reset(current_->song.tracks[static_cast<std::size_t>(t)].gain);
        }
    }
    if (voices_covered_by(*current_)) {
        released_.push(old); // free_space checked above
    } else {
        retired_[static_cast<std::size_t>(retired_count_++)] = old; // a voice still plays its sample
    }
}

void Engine::transport(TransportAction a) noexcept {
    const seq::CompiledSong& song = current_->song;
    switch (a) {
    case TransportAction::Play:
    case TransportAction::PlayRecord:
        flush_sequencer_events();
        reset_automation();
        seq_.play(song, block_frame_, a == TransportAction::PlayRecord);
        // Loops already running restart with the song, on its first beat.
        for (int i = 0; i < model::kLoopSlots; ++i) {
            if (loops_.active(i)) {
                loops_.launch(i, seq_.next_grid_frame(song, block_frame_, sample_rate_, 1));
            }
        }
        break;
    case TransportAction::Continue:
        seq_.resume(song, block_frame_, sample_rate_);
        break;
    case TransportAction::ToggleRecord:
        if (seq_.playing()) {
            seq_.set_recording(!seq_.armed());
        } else {
            flush_sequencer_events();
            seq_.play(song, block_frame_, true);
        }
        break;
    case TransportAction::Stop:
        seq_.stop(song, block_frame_, sample_rate_);
        flush_sequencer_events();
        voices_.release_all(); // sequencer notes whose note-off was just discarded
        loops_.stop_all(block_frame_);
        out_transport(block_frame_, 0xFC);    // MIDI stop
        out_notes_off(block_frame_, false);   // external notes whose note-off was discarded
        reset_automation();
        break;
    }
}

void Engine::publish_transport() noexcept {
    t_state_.store(static_cast<std::uint8_t>(seq_.state()), std::memory_order_relaxed);
    t_recording_.store(seq_.recording(), std::memory_order_relaxed);
    t_section_.store(seq_.section(), std::memory_order_relaxed);
    t_step_.store(seq_.step(), std::memory_order_relaxed);
    t_pattern_.store(seq_.pattern(current_->song), std::memory_order_relaxed);
    t_tick_.store(seq_.tick_at(current_->song, block_frame_, sample_rate_), std::memory_order_relaxed);
}

TransportInfo Engine::read_transport() const {
    TransportInfo t;
    t.state = static_cast<seq::Sequencer::State>(t_state_.load(std::memory_order_relaxed));
    t.recording = t_recording_.load(std::memory_order_relaxed);
    t.section = t_section_.load(std::memory_order_relaxed);
    t.step = t_step_.load(std::memory_order_relaxed);
    t.pattern = t_pattern_.load(std::memory_order_relaxed);
    t.tick = t_tick_.load(std::memory_order_relaxed);
    return t;
}

LoopInfo Engine::read_loops() const {
    LoopInfo info;
    for (std::size_t i = 0; i < info.state.size(); ++i) {
        info.state[i] = static_cast<LoopState>(loop_state_[i].load(std::memory_order_relaxed));
        info.phase[i] = loop_phase_[i].load(std::memory_order_relaxed);
    }
    return info;
}

std::array<float, kBusCount> Engine::read_track_peaks() {
    std::array<float, kBusCount> p{};
    for (std::size_t b = 0; b < p.size(); ++b) {
        p[b] = bus_peak_[b].exchange(0.0f, std::memory_order_relaxed);
    }
    return p;
}

} // namespace phos::engine
