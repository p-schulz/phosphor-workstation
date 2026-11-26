#pragma once

#include <cstdint>
#include <string>

#include "model/sample_data.h"

// Page L loop slots (PLAN.md §7.5): a sample, its length in bars and how it follows the tempo.
namespace phos::model {

inline constexpr int kLoopSlots = 8;
// LOOP tracks on Page R: note C4 (60) launches slot 1 ... G4 (67) slot 8; others wrap by 8.
inline constexpr int kLoopBaseNote = 60;

enum class LoopSync : std::uint8_t {
    Varispeed, // rate = projectBPM / loopBPM, the pitch follows: the authentic slowed sound
    Stretch,   // pitch kept (Signalsmith Stretch, cached per tempo); SHIFT transposes
    Slice,     // SLICES equal slices retriggered on the grid; follows the tempo without artefacts
};
enum class LaunchQuantize : std::uint8_t { Immediate, Beat, Bar };

const char* loop_sync_name(LoopSync s);          // "VARI", "STRETCH", "SLICE"
const char* launch_quantize_name(LaunchQuantize q); // "NOW", "BEAT", "BAR"

struct LoopSlot {
    std::string name = "EMPTY";
    SampleRef sample;
    int bars = 1;            // 1..64, length of the sample in bars
    int beats_per_bar = 4;   // quarter notes per bar of the loop (1..16)
    LoopSync sync = LoopSync::Varispeed;
    LaunchQuantize quantize = LaunchQuantize::Bar;
    float shift = 0.0f;      // STRETCH: semitones, -24..24
    int slices = 16;         // SLICE: 2..64
    float gain_db = 0.0f;    // -60..+12
    float lp_hz = 20000.0f;  // low-pass, 200..20000 (20000 = open)
    bool reverse = false;
    float screw = 1.0f;      // 0.5..1.0: continuous slow-down, pitch follows
    int track = 8;           // mixer track the slot plays on (0..15)

    double beats() const { return static_cast<double>(bars) * beats_per_bar; }
    // Tempo the sample has at its stated length; 0 without a sample.
    double loop_bpm() const {
        return sample && sample->frames ? beats() * 60.0 * sample->sample_rate / static_cast<double>(sample->frames) : 0.0;
    }
    friend bool operator==(const LoopSlot&, const LoopSlot&) = default;
};

// Bars for a sample of `seconds` at `bpm` (4/4): the power of two (1..64) whose tempo lies
// closest to `bpm`, so a loop never plays more than ~1.41x off its own speed.
int detect_bars(double seconds, double bpm);

} // namespace phos::model
