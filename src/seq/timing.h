#pragma once

#include <cstdint>
#include <string>

// Musical time <-> sample frames (PLAN.md §6.4, §7.1). Exact integer arithmetic: one tick at
// 960 PPQ lasts rate * 60 / (bpm * 960) frames = rate * 5 / (bpm10 * 8) frames with the tempo in
// tenths of a BPM, so frame_of_tick() is a single floor division of 64-bit integers. Positions are
// always measured from a pattern pass start; nothing accumulates, so long playback cannot drift.
namespace phos::seq {

// First frame at or after tick `tick` (floor of the exact rational value).
constexpr std::uint64_t frame_of_tick(std::uint64_t tick, std::uint32_t rate, int bpm10) {
    return tick * rate * 5u / (static_cast<std::uint64_t>(bpm10) * 8u);
}

// Tick that is sounding at frame `frame` (the last tick whose frame_of_tick() <= frame).
constexpr std::uint64_t tick_at_frame(std::uint64_t frame, std::uint32_t rate, int bpm10) {
    // frame_of_tick(t) <= frame  <=>  t * rate * 5 < (frame + 1) * bpm10 * 8
    const std::uint64_t den = static_cast<std::uint64_t>(rate) * 5u;
    const std::uint64_t num = (frame + 1) * static_cast<std::uint64_t>(bpm10) * 8u;
    return (num - 1) / den;
}

constexpr int beat_ticks(int sig_den) { return 960 * 4 / sig_den; }
constexpr int bar_ticks(int sig_num, int sig_den) { return sig_num * beat_ticks(sig_den); }

// Swing (PLAN.md §7.1): within each pair of `grid` steps, the first half [0, grid) is stretched to
// [0, grid * 2 * percent / 100) and the second half compressed after it. 50 % = straight.
std::uint32_t swing_tick(std::uint32_t tick, int grid_ticks, int percent);

// "001:1:000" (bar from 1, beat from 1, ticks within the beat).
std::string bar_beat_tick(std::uint64_t tick, int sig_num, int sig_den);

} // namespace phos::seq
