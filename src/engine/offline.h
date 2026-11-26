#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model/workspace.h"

// Offline rendering through a private engine (PLAN.md §7.5 RESAMPLE: "render a pattern into a new
// sample via the offline engine path"). Same code as live playback, deterministic, faster than
// real time. Runs on the calling thread (UI or worker); allocates.
namespace phos::engine {

struct OfflineRender {
    int section = 0;          // played in PATTERN mode, metronome and count-in off
    int passes = 1;           // pattern passes to render
    double tail_s = 0.0;      // extra time after the last pass (reverb / release tails)
    bool master = true;       // false: bypass master inserts and VAPOR (a dry resample)
    // A loop: one extra pass is rendered first and dropped, so release and reverb tails of the
    // pass before are in the result and it loops without a seam.
    bool seamless = false;
    std::uint32_t sample_rate = 48000;
    std::string name = "RESAMPLE";
};

// Stereo sample of the rendered section. Loops in STRETCH mode get their cache first.
model::SampleRef render_section(const model::Document& doc, const OfflineRender& opt);

// RENDER / --render-audio (PLAN.md §7.9): the song as it plays, from the start. SONG mode renders
// the order once (every section x its repeats), PATTERN mode the current section x its repeats.
// Bit-identical for the same document and rate (no host clock, fixed blocks, fresh engine).
struct SongRender {
    double tail_s = 2.0; // release and reverb tails after the end
    bool master = true;
    bool stems = false;  // also one render per track with everything else muted
    std::uint32_t sample_rate = 48000;
};
struct Stem {
    int track = 0;
    std::vector<float> left;
    std::vector<float> right;
};
struct Render {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<Stem> stems;
};
Render render_song(const model::Document& doc, const SongRender& opt);
// Frames the song (or section, in PATTERN mode) lasts, without tail.
std::uint64_t song_frames(const model::Song& song, std::uint32_t sample_rate);
// Tracks that make sound (notes in a pattern that plays on a NOTES track, or a loop slot playing
// on it) and are not muted.
std::vector<int> stem_tracks(const model::Song& song);

} // namespace phos::engine
