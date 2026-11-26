#pragma once

#include <algorithm>
#include <cstdint>

#include "seq/compiled_song.h"
#include "seq/timing.h"

// Transport and pattern playback on the audio thread (PLAN.md §7.1). Pure state machine: no
// allocation, no I/O; it reads a CompiledSong and emits note and click events with exact frames.
//
// Timing: every frame is anchor_frame_ + frame_of_tick(pass_tick_ + tick), where pass_tick_ counts
// the ticks of all passes since the anchor. Pass boundaries are therefore never rounded on their
// own and long playback cannot drift. Only a tempo change sets a new anchor.
namespace phos::seq {

// Cc: note = controller, velocity = value. Clock: 24 PPQN (every 40 ticks). PatternStart: a pass
// begins (program changes). Start / Continue: playback begins after count-in / resumes; Continue
// carries the song position in sixteenths, note = low 7 bits, velocity = high 7 bits.
enum class SeqEventType : std::uint8_t { NoteOn, NoteOff, Click, AccentClick, Cc, Clock, PatternStart, Start, Continue };
inline constexpr std::uint32_t kClockTicks = 40; // 960 PPQ / 24

struct SeqEvent {
    std::uint64_t frame = 0;
    SeqEventType type = SeqEventType::NoteOn;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint8_t track = 0;
};

class Sequencer {
public:
    enum class State : std::uint8_t { Stopped, CountIn, Playing };

    // Starts at the beginning (song step 0, or the current section in PATTERN mode) at `frame`.
    void play(const CompiledSong& s, std::uint64_t frame, bool record);
    // Continues from the stopped position.
    void resume(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate);
    // Stops and remembers the position for resume().
    void stop(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate);
    void set_recording(bool on) { recording_ = on; }

    State state() const { return state_; }
    bool playing() const { return state_ != State::Stopped; }
    bool recording() const { return recording_ && state_ == State::Playing; }
    bool armed() const { return recording_; }
    int section() const { return section_; }
    int step() const { return step_; }
    int pattern(const CompiledSong& s) const { return s.sections[static_cast<std::size_t>(section_)].pattern; }
    int bpm10() const { return bpm10_; }

    // Tick within the current pattern at `frame` (a frame just before the pass wraps to the end of
    // the previous pass). Count-in reports 0, stopped the stop position.
    std::uint32_t tick_at(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate) const;

    // First frame >= `frame` on a multiple of `unit_ticks` from the pass start (loop launch quantize).
    // Stopped: `frame` itself; count-in: the first frame of the song.
    std::uint64_t next_grid_frame(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate,
                                  std::uint32_t unit_ticks) const;

    // Re-aligns indices after the song changed (new snapshot) without moving the playhead.
    void resync(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate);

    // Emits every event in [frame, frame + n). `emit(const SeqEvent&)`.
    template <typename Emit>
    void advance(const CompiledSong& s, std::uint64_t frame, std::uint32_t n, std::uint32_t rate, Emit&& emit) {
        if (state_ == State::Stopped) {
            return;
        }
        const std::uint64_t end = frame + n;
        for (int guard = 0; guard < 64; ++guard) { // bounded: one iteration per pattern pass
            const SeqSection& sec = section_data(s);
            const SeqPattern& pat = pattern_data(s);
            if (sec.bpm10 != bpm10_) {
                reanchor(frame, rate, sec.bpm10);
            }
            const std::uint32_t pass_ticks = state_ == State::CountIn ? count_in_ticks(s) : pat.length_ticks;
            const std::uint64_t pass_end = frame_at(pass_ticks, rate);
            const std::uint64_t win_end = std::min(end, pass_end);
            if (state_ == State::Playing) {
                if (transport_pending_ != SeqEventType::NoteOn) { // Start or Continue
                    const std::uint64_t at = transport_pending_ == SeqEventType::Start ? std::max(frame_at(0, rate), frame) : frame;
                    emit(SeqEvent{at, transport_pending_, static_cast<std::uint8_t>(spp_ & 0x7F),
                                  static_cast<std::uint8_t>((spp_ >> 7) & 0x7F), 0});
                    transport_pending_ = SeqEventType::NoteOn;
                }
                if (pass_start_pending_) {
                    emit(SeqEvent{std::max(frame_at(0, rate), frame), SeqEventType::PatternStart, 0, 0, 0});
                    pass_start_pending_ = false;
                }
                for (; cc_cursor_ < pat.cc_end; ++cc_cursor_) {
                    const SeqCc& cc = s.ccs[cc_cursor_];
                    const std::uint64_t at = frame_at(cc.tick, rate);
                    if (at >= win_end) {
                        break;
                    }
                    emit(SeqEvent{std::max(at, frame), SeqEventType::Cc, cc.cc, cc.value, cc.track});
                }
                for (; clock_tick_ < pass_ticks; clock_tick_ += kClockTicks) {
                    const std::uint64_t at = frame_at(clock_tick_, rate);
                    if (at >= win_end) {
                        break;
                    }
                    if (at >= frame) {
                        emit(SeqEvent{at, SeqEventType::Clock, 0, 0, 0});
                    }
                }
                for (; cursor_ < pat.end; ++cursor_) {
                    const SeqNote& note = s.notes[cursor_];
                    const std::uint64_t at = frame_at(note.tick, rate);
                    if (at >= win_end) {
                        break;
                    }
                    const std::uint64_t off = frame_at(std::min(note.off_tick, pass_ticks), rate);
                    emit(SeqEvent{std::max(at, frame), SeqEventType::NoteOn, note.note, note.velocity, note.track});
                    emit(SeqEvent{std::max(off, frame), SeqEventType::NoteOff, note.note, 0, note.track});
                }
            }
            if (clicks_on(s)) {
                const auto beat = static_cast<std::uint32_t>(beat_ticks(sec.sig_den));
                const auto bar = static_cast<std::uint32_t>(bar_ticks(sec.sig_num, sec.sig_den));
                for (; click_tick_ < pass_ticks; click_tick_ += beat) {
                    const std::uint64_t at = frame_at(click_tick_, rate);
                    if (at >= win_end) {
                        break;
                    }
                    if (at >= frame) {
                        const bool accent = click_tick_ % bar == 0;
                        emit(SeqEvent{at, accent ? SeqEventType::AccentClick : SeqEventType::Click, 0,
                                      accent ? std::uint8_t{127} : std::uint8_t{80}, 0});
                    }
                }
            }
            if (win_end == end && end <= pass_end) {
                return; // the block ends inside (or exactly at the end of) this pass
            }
            next_pass(s, pass_ticks, pass_end);
            frame = std::max(frame, pass_end);
        }
    }

private:
    const SeqSection& section_data(const CompiledSong& s) const { return s.sections[static_cast<std::size_t>(section_)]; }
    const SeqPattern& pattern_data(const CompiledSong& s) const {
        return s.patterns[static_cast<std::size_t>(
            std::clamp(section_data(s).pattern, 0, static_cast<int>(s.patterns.size()) - 1))];
    }
    std::uint64_t frame_at(std::uint64_t tick_in_pass, std::uint32_t rate) const {
        return anchor_frame_ + frame_of_tick(pass_tick_ + tick_in_pass, rate, bpm10_);
    }
    std::uint32_t count_in_ticks(const CompiledSong& s) const;
    bool clicks_on(const CompiledSong& s) const;
    void reanchor(std::uint64_t frame, std::uint32_t rate, int new_bpm10);
    void reset_cursors(const CompiledSong& s);
    void next_pass(const CompiledSong& s, std::uint32_t pass_ticks, std::uint64_t pass_end);
    int section_for_step(const CompiledSong& s, int step) const;

    State state_ = State::Stopped;
    bool recording_ = false;
    int step_ = 0;
    int section_ = 0;
    int repeat_ = 0;
    int bpm10_ = 1200;
    std::uint64_t anchor_frame_ = 0;
    std::uint64_t pass_tick_ = 0;  // ticks from the anchor to the start of the current pass
    std::uint32_t cursor_ = 0;     // next note index
    std::uint32_t click_tick_ = 0; // next metronome tick within the pass
    std::uint32_t stopped_tick_ = 0;
    // M8: MIDI out.
    std::uint32_t cc_cursor_ = 0;       // next automation value
    std::uint32_t clock_tick_ = 0;      // next clock tick within the pass (carries over passes)
    std::uint64_t song_ticks_ = 0;      // ticks of the passes played since PLAY (for SPP)
    std::uint32_t spp_ = 0;             // song position of a pending Continue, sixteenths
    bool pass_start_pending_ = false;
    SeqEventType transport_pending_ = SeqEventType::NoteOn; // NoteOn = none
};

} // namespace phos::seq
