#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "model/workspace.h"

// Standard MIDI files (PLAN.md §7.9): export type 1 at 960 PPQ with tempo and time-signature meta
// events; import type 0 / 1 into patterns. Written from the SMF 1.0 specification (MMA).
namespace phos::io {

inline constexpr int kSmfPpq = 960;

// The song as it plays (SONG mode: the order once, every section x its repeats; PATTERN mode: the
// current section x its repeats). Track 0 holds the name, tempo and time signature (at every
// section start where they change); then one track per sounding NOTES track with its name,
// program change (if set), notes (swing applied) and CC automation, on the track's MIDI channel.
std::vector<std::uint8_t> export_smf(const model::Document& doc, const std::string& title);
bool write_smf(const std::filesystem::path& path, const model::Document& doc, const std::string& title,
               std::string* error);

// What the file says about one imported track (index = PHOSPHOR track).
struct SmfTrackInfo {
    std::string name;
    int program = -1;   // first program change, -1 = none
    int channel = 0;    // MIDI channel 0..15
    bool drums = false; // channel 10
};

struct SmfImport {
    std::vector<SmfTrackInfo> track_info;
    int ccs = 0; // controller values imported into automation lanes (CC 1, 7, 10, 11, 74)
    model::Song song;   // the base song with patterns, sections, order and track names replaced
    int tracks = 0;     // PHOSPHOR tracks that got notes
    int notes = 0;
    int sections = 0;
    std::string warning; // e.g. "TRUNCATED AFTER 104 BARS", "MORE THAN 16 TRACKS"
};

// Notes are cut into patterns of `bars_per_pattern` bars (sections A.. in order, SONG mode).
// Type 1: each MTrk with notes becomes the next track; type 0: one track per MIDI channel. Tracks
// keep their channel and program (for MIDI out); CC 1, 7, 10, 11 and 74 become automation lanes.
std::optional<SmfImport> import_smf(const std::vector<std::uint8_t>& bytes, const model::Song& base,
                                    std::string* error, int bars_per_pattern = 4);
bool is_midi_file(const std::filesystem::path& path); // .mid / .midi
std::optional<SmfImport> read_smf(const std::filesystem::path& path, const model::Song& base, std::string* error);

} // namespace phos::io
