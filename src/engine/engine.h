#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/spsc_ring.h"
#include "core/time_dll.h"
#include "dsp/limiter.h"
#include "dsp/smoother.h"
#include "engine/audio_backend.h"
#include "engine/latency_stats.h"
#include "engine/loop_player.h"
#include "engine/messages.h"
#include "engine/midi_input.h"
#include "engine/snes_bus.h"
#include "engine/voice_pool.h"
#include "engine/xrun_detector.h"
#include "seq/sequencer.h"

namespace phos::engine {

// Values the UI reads once per frame (PLAN.md §6.3: audio -> UI via atomics).
struct EngineMeters {
    float peak_l = 0.0f;           // linear peak since the last read
    float peak_r = 0.0f;
    float gain_reduction_db = 0.0f; // limiter, worst since the last read (<= 0)
    float cpu = 0.0f;              // smoothed process time / block time
    float cpu_peak = 0.0f;         // worst block since start (or reset_counters)
    int voices = 0;
    std::uint64_t callbacks = 0;
    std::uint64_t frames = 0;
    std::uint64_t xruns = 0;       // late callbacks (XrunDetector)
    std::uint64_t overloads = 0;   // blocks whose processing took longer than their duration
    std::uint64_t dropped_commands = 0;
    std::uint32_t sample_rate = 0;
    std::uint32_t last_block = 0;  // frames in the most recent callback
};

struct MidiMeters {
    std::uint64_t events = 0;         // MIDI messages handled (all inputs + KEYJAZZ)
    std::uint64_t note_ons = 0;
    std::uint64_t queue_overflow = 0; // lost in the input queues (sum over slots)
    std::uint64_t late = 0;           // STABLE events whose slot had already passed
    std::uint64_t schedule_overflow = 0; // played early (at once) because the scheduler was full
    std::array<std::uint64_t, 2> held{}; // bitmap of notes currently held (0..127)
    bool held_note(int n) const { return (held[static_cast<std::size_t>(n >> 6)] >> (n & 63)) & 1u; }
};

// Transport position for the status line and Page R (read once per frame).
struct TransportInfo {
    seq::Sequencer::State state = seq::Sequencer::State::Stopped;
    bool recording = false;
    int section = 0;
    int step = 0;
    int pattern = 0;
    std::uint32_t tick = 0; // within the pattern pass, at the last block start
};

// Page L loop slots as the UI reads them once per frame.
struct LoopInfo {
    std::array<LoopState, model::kLoopSlots> state{};
    std::array<float, model::kLoopSlots> phase{}; // 0..1 through the loop
};

// Audio-thread entry point (PLAN.md §6). Everything reachable from process() obeys the realtime
// contract: no allocation, locks, I/O or exceptions; buffers are sized in the constructor.
class Engine final : public AudioCallback {
public:
    static constexpr std::uint32_t kMaxBlock = 2048;
    static constexpr std::size_t kCommandCapacity = 1024;
    static constexpr std::size_t kSnapshotCapacity = 16;
    static constexpr std::size_t kScheduleCapacity = 8192;

    Engine();
    ~Engine() override;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // --- UI thread -------------------------------------------------------------------------
    // False when the command queue is full (counted in dropped_commands).
    bool send(const EngineCommand& command);
    // Hands a snapshot to the audio thread. False (snapshot destroyed) when the queue is full.
    bool publish(std::unique_ptr<EngineSnapshot> snapshot);
    // Frees snapshots the audio thread has released. Returns how many were freed.
    int collect_garbage();
    EngineMeters read_meters();
    std::uint32_t sample_rate() const { return rate_.load(std::memory_order_relaxed); }
    MidiMeters read_midi_meters() const;
    LatencyStats::View latency() const { return latency_.view(); }
    void reset_latency() { latency_.request_reset(); }
    bool pop_cc(CcEvent& out) { return cc_events_.pop(out); }
    bool pop_rec(RecEvent& out) { return rec_events_.pop(out); }
    TransportInfo read_transport() const;
    // Post-fader peak per bus since the last read (16 tracks + direct bus).
    std::array<float, kBusCount> read_track_peaks();
    LoopInfo read_loops() const;
    // MIDI-out thread: next queued message (single consumer).
    bool pop_midi_out(MidiOutEvent& out) { return midi_out_.pop(out); }
    std::uint64_t midi_out_overflow() const { return midi_out_lost_.load(std::memory_order_relaxed); }
    // Host time (steady_clock ns) at which engine frame `frame` is rendered, from the DLL; false
    // until the clock has locked (or offline). Any thread.
    bool frame_to_ns(std::uint64_t frame, double& ns) const;
    void reset_counters();

    // --- MIDI-in callbacks (one producer per slot) -----------------------------------------
    MidiInputQueues& midi_queues() { return *midi_; }

    // --- AudioCallback ---------------------------------------------------------------------
    void prepare(std::uint32_t sample_rate, std::uint32_t channels) override;
    void process(float* out, std::uint32_t frames, std::uint32_t channels, std::int64_t host_ns) noexcept override;

    // Offline rendering on the calling thread (tests, --render-test). Same code path as the device;
    // without a host clock every event is placed IMMEDIATE.
    void render_offline(float* out, std::uint32_t frames, std::uint32_t channels) noexcept {
        process(out, frames, channels, 0);
    }

private:
    struct Scheduled {
        std::uint64_t frame = 0;
        std::uint8_t status = 0;
        std::uint8_t data1 = 0;
        std::uint8_t data2 = 0;
        std::uint8_t port = 0;
        std::uint8_t track = 0; // sequencer events
    };

    void apply_snapshot(const EngineSnapshot& s) noexcept;
    void swap_snapshot() noexcept;
    bool voices_covered_by(const EngineSnapshot& s) const noexcept;
    void handle(const EngineCommand& c) noexcept;
    // engine_mix.cpp
    void render_block(float* out, std::uint32_t frames, std::uint32_t channels, std::uint64_t frame) noexcept;
    void mix_tracks(float* l, float* r, std::uint32_t frames, double bpm) noexcept;
    void run_fx(dsp::FxUnit* unit, const model::FxSlot& slot, float* l, float* r, std::uint32_t frames,
                double bpm) noexcept;
    void master_chain(float* l, float* r, std::uint32_t frames, double bpm) noexcept;
    std::uint64_t launch_frame(int slot, std::uint64_t now) const noexcept;

    // engine_midi_out.cpp
    void out(std::uint64_t frame, int slot, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint8_t size) noexcept;
    bool sequencer_system(const Scheduled& e) noexcept; // clock, start, continue, programs, CC
    void automation(int track, std::uint8_t cc, std::uint8_t value) noexcept;
    void reset_automation() noexcept;
    void out_notes_off(std::uint64_t frame, bool panic) noexcept;
    void out_transport(std::uint64_t frame, std::uint8_t status) noexcept;
    bool thru(const Scheduled& e) noexcept; // true: the current track is EXTERNAL only
    void publish_clock() noexcept;
    void loop_command(int slot, int action, std::uint64_t now) noexcept;
    void transport(TransportAction a) noexcept;
    void publish_transport() noexcept;

    // engine_midi.cpp
    void schedule_midi(std::uint8_t status, std::uint8_t d1, std::uint8_t d2, std::uint8_t port, std::int64_t ns,
                       std::uint32_t frames, bool host_clock) noexcept;
    void drain_midi(std::uint32_t frames, bool host_clock) noexcept;
    void apply_due(std::uint64_t frame) noexcept;
    std::uint64_t next_due() const noexcept;
    void apply_midi(const Scheduled& e) noexcept;
    void enqueue(const Scheduled& e) noexcept;
    void schedule_sequencer(const seq::SeqEvent& ev) noexcept;
    void record(bool on, std::uint8_t note, std::uint8_t velocity, std::uint64_t frame) noexcept;
    void flush_sequencer_events() noexcept;
    float velocity_gain(std::uint8_t velocity) const noexcept;

    // Owned by the audio thread once published; the constructor creates the first one.
    EngineSnapshot* current_ = nullptr;
    // Replaced snapshots whose samples some voice still plays (released once that ends).
    static constexpr int kRetired = 8;
    std::array<EngineSnapshot*, kRetired> retired_{};
    int retired_count_ = 0;
    SpscRing<EngineSnapshot*, kSnapshotCapacity> incoming_;
    SpscRing<EngineSnapshot*, kSnapshotCapacity * 2> released_;
    SpscRing<EngineCommand, kCommandCapacity> commands_;
    SpscRing<CcEvent, 256> cc_events_;
    SpscRing<RecEvent, 4096> rec_events_;
    std::unique_ptr<MidiInputQueues> midi_; // ~2 MB: heap, not stack

    std::uint32_t sample_rate_ = 48000;
    std::uint64_t block_frame_ = 0; // engine frame counter (PLAN.md §6.4)
    TimeDll dll_;
    std::vector<float> left_;
    std::vector<float> right_;
    std::vector<float> bus_audio_; // kBusCount x 2 x kMaxBlock
    std::array<float*, kBusCount> bus_l_{};
    std::array<float*, kBusCount> bus_r_{};
    std::array<dsp::OnePoleSmoother, kTrackCount> track_gain_{};
    std::vector<float> return_audio_; // 2 returns x 2 x kMaxBlock
    std::array<float*, 4> ret_{};     // A left, A right, B left, B right
    LoopPlayer loops_;
    std::array<std::atomic<std::uint8_t>, model::kLoopSlots> loop_state_{};
    std::array<std::atomic<float>, model::kLoopSlots> loop_phase_{};
    SpscRing<MidiOutEvent, 8192> midi_out_;
    std::atomic<std::uint64_t> midi_out_lost_{0};
    std::array<float, kTrackCount> auto_level_{}; // CC 7 x CC 11 automation, 1 = none
    std::array<float, kTrackCount> auto_volume_{};
    std::array<float, kTrackCount> auto_expr_{};
    std::array<float, kTrackCount> auto_pan_{};   // CC 10, NaN = the track's pan
    // Seqlock: the DLL state for frame_to_ns() on other threads.
    std::atomic<std::uint32_t> clock_seq_{0};
    std::atomic<double> clock_t0_{0.0};
    std::atomic<std::uint64_t> clock_frame0_{0};
    std::atomic<double> clock_npf_{0.0};
    std::atomic<bool> clock_locked_{false};
    seq::Sequencer seq_;
    SnesBus snes_bus_;
    bool song_changed_ = false;
    std::vector<Scheduled> schedule_; // sorted by frame; sized in the constructor
    std::size_t sched_head_ = 0;
    std::size_t sched_size_ = 0;
    VoicePool voices_;
    dsp::OnePoleSmoother master_gain_;
    dsp::PeakLimiter limiter_;
    XrunDetector xruns_;
    LatencyStats latency_;
    float cpu_smoothed_ = 0.0f;
    std::array<std::uint64_t, 2> held_local_{};

    std::atomic<float> peak_l_{0.0f};
    std::atomic<float> peak_r_{0.0f};
    std::atomic<float> min_gain_{1.0f};
    std::atomic<float> cpu_{0.0f};
    std::atomic<float> cpu_peak_{0.0f};
    std::atomic<int> voice_count_{0};
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<std::uint64_t> xrun_count_{0};
    std::atomic<std::uint64_t> overloads_{0};
    std::atomic<std::uint32_t> rate_{0};
    std::atomic<std::uint32_t> last_block_{0};
    std::atomic<std::uint64_t> midi_events_{0};
    std::atomic<std::uint64_t> note_ons_{0};
    std::atomic<std::uint64_t> late_events_{0};
    std::atomic<std::uint64_t> sched_overflow_{0};
    std::array<std::atomic<std::uint64_t>, 2> held_{};
    std::array<std::atomic<float>, kBusCount> bus_peak_{};
    std::atomic<std::uint8_t> t_state_{0};
    std::atomic<bool> t_recording_{false};
    std::atomic<int> t_section_{0};
    std::atomic<int> t_step_{0};
    std::atomic<int> t_pattern_{0};
    std::atomic<std::uint32_t> t_tick_{0};
    std::uint64_t dropped_commands_ = 0; // UI thread only
};

} // namespace phos::engine
