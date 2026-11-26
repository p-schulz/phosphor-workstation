#include "seq/sequencer.h"

namespace phos::seq {

int Sequencer::section_for_step(const CompiledSong& s, int step) const {
    if (s.play_mode == model::PlayMode::Pattern) {
        return s.current_section;
    }
    return s.order[static_cast<std::size_t>(std::clamp(step, 0, static_cast<int>(s.order.size()) - 1))];
}

std::uint32_t Sequencer::count_in_ticks(const CompiledSong& s) const {
    const SeqSection& sec = section_data(s);
    return static_cast<std::uint32_t>(std::max(1, s.count_in_bars) * bar_ticks(sec.sig_num, sec.sig_den));
}

bool Sequencer::clicks_on(const CompiledSong& s) const {
    if (state_ == State::CountIn) {
        return true;
    }
    return s.metronome == model::MetronomeMode::Play || (s.metronome == model::MetronomeMode::Record && recording_);
}

void Sequencer::reset_cursors(const CompiledSong& s) {
    cursor_ = pattern_data(s).begin;
    cc_cursor_ = pattern_data(s).cc_begin;
    click_tick_ = 0;
    pass_start_pending_ = true;
}

void Sequencer::play(const CompiledSong& s, std::uint64_t frame, bool record) {
    recording_ = record;
    step_ = 0;
    repeat_ = 0;
    section_ = section_for_step(s, 0);
    state_ = record && s.count_in_bars > 0 ? State::CountIn : State::Playing;
    bpm10_ = section_data(s).bpm10;
    anchor_frame_ = frame;
    pass_tick_ = 0;
    reset_cursors(s);
    clock_tick_ = 0;
    song_ticks_ = 0;
    spp_ = 0;
    transport_pending_ = SeqEventType::Start;
}

void Sequencer::stop(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate) {
    if (state_ == State::Playing) {
        stopped_tick_ = tick_at(s, frame, rate);
    }
    state_ = State::Stopped;
    recording_ = false;
}

void Sequencer::resume(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate) {
    if (state_ != State::Stopped) {
        return;
    }
    state_ = State::Playing;
    section_ = section_for_step(s, step_);
    bpm10_ = section_data(s).bpm10;
    stopped_tick_ %= pattern_data(s).length_ticks;
    // Anchor so that the stopped tick sounds at `frame`.
    const std::uint64_t into = frame_of_tick(stopped_tick_, rate, bpm10_);
    anchor_frame_ = frame >= into ? frame - into : 0;
    pass_tick_ = 0;
    resync(s, frame, rate);
    spp_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(16383, (song_ticks_ + stopped_tick_) / (model::kPpq / 4)));
    transport_pending_ = SeqEventType::Continue;
}

void Sequencer::next_pass(const CompiledSong& s, std::uint32_t pass_ticks, std::uint64_t pass_end) {
    pass_tick_ += pass_ticks;
    if (state_ == State::CountIn) {
        state_ = State::Playing;
        clock_tick_ = 0;
    } else {
        song_ticks_ += pass_ticks;
        clock_tick_ = clock_tick_ >= pass_ticks ? clock_tick_ - pass_ticks : 0; // keep the 24 PPQN grid
        if (s.play_mode == model::PlayMode::Song && ++repeat_ >= section_data(s).repeats) {
            repeat_ = 0;
            step_ = (step_ + 1) % static_cast<int>(s.order.size());
        }
    }
    section_ = section_for_step(s, step_);
    if (section_data(s).bpm10 != bpm10_) {
        // New tempo from this pass on: anchor at the boundary (exact by construction).
        bpm10_ = section_data(s).bpm10;
        anchor_frame_ = pass_end;
        pass_tick_ = 0;
    }
    reset_cursors(s);
}

void Sequencer::reanchor(std::uint64_t frame, std::uint32_t rate, int new_bpm10) {
    // Keep the musical position: the tick sounding at `frame` stays at `frame` in the new tempo.
    const std::uint64_t pass_start = frame_at(0, rate);
    const std::uint64_t tick = frame >= pass_start ? tick_at_frame(frame - anchor_frame_, rate, bpm10_) - pass_tick_ : 0;
    bpm10_ = new_bpm10;
    const std::uint64_t into = frame_of_tick(tick, rate, bpm10_);
    anchor_frame_ = frame >= into ? frame - into : 0;
    pass_tick_ = 0;
}

std::uint32_t Sequencer::tick_at(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate) const {
    if (state_ != State::Playing) {
        return state_ == State::Stopped ? stopped_tick_ : 0;
    }
    const std::uint32_t len = pattern_data(s).length_ticks;
    const std::uint64_t pass_start = frame_at(0, rate);
    if (frame >= pass_start) {
        return static_cast<std::uint32_t>((tick_at_frame(frame - anchor_frame_, rate, bpm10_) - pass_tick_) % len);
    }
    // Slightly before this pass (a key pressed just before the loop point): end of the last one.
    const std::uint64_t back = tick_at_frame(pass_start - frame - 1, rate, bpm10_) + 1;
    return static_cast<std::uint32_t>((len - back % len) % len);
}

std::uint64_t Sequencer::next_grid_frame(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate,
                                         std::uint32_t unit_ticks) const {
    if (state_ == State::Stopped || unit_ticks == 0) {
        return frame;
    }
    if (state_ == State::CountIn) {
        return std::max(frame, frame_at(count_in_ticks(s), rate));
    }
    const std::uint64_t pass_start = frame_at(0, rate);
    if (frame <= pass_start) {
        return pass_start;
    }
    const std::uint64_t tick = tick_at_frame(frame - anchor_frame_, rate, bpm10_) - pass_tick_;
    std::uint64_t next = (tick + unit_ticks - 1) / unit_ticks * unit_ticks;
    for (int guard = 0; guard < 4 && frame_at(next, rate) < frame; ++guard) {
        next += unit_ticks;
    }
    return frame_at(next, rate);
}

void Sequencer::resync(const CompiledSong& s, std::uint64_t frame, std::uint32_t rate) {
    if (step_ >= static_cast<int>(s.order.size())) {
        step_ = 0;
    }
    section_ = section_for_step(s, step_);
    if (section_data(s).bpm10 != bpm10_) {
        reanchor(frame, rate, section_data(s).bpm10);
    }
    const SeqPattern& pat = pattern_data(s);
    const std::uint64_t pass_start = frame_at(0, rate);
    std::uint64_t tick = frame >= pass_start ? tick_at_frame(frame - anchor_frame_, rate, bpm10_) - pass_tick_ : 0;
    if (tick >= pat.length_ticks) { // the pattern got shorter: wrap into it
        tick %= pat.length_ticks;
        const std::uint64_t into = frame_of_tick(tick, rate, bpm10_);
        anchor_frame_ = frame >= into ? frame - into : 0;
        pass_tick_ = 0;
    }
    cursor_ = pat.begin;
    while (cursor_ < pat.end && frame_at(s.notes[cursor_].tick, rate) < frame) {
        ++cursor_;
    }
    cc_cursor_ = pat.cc_begin;
    while (cc_cursor_ < pat.cc_end && frame_at(s.ccs[cc_cursor_].tick, rate) < frame) {
        ++cc_cursor_;
    }
    clock_tick_ = static_cast<std::uint32_t>((tick + kClockTicks - 1) / kClockTicks * kClockTicks);
    pass_start_pending_ = false;
    const auto beat = static_cast<std::uint32_t>(beat_ticks(section_data(s).sig_den));
    click_tick_ = 0;
    while (frame_at(click_tick_, rate) < frame) {
        click_tick_ += beat;
    }
}

} // namespace phos::seq
