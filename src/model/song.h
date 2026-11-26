#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "model/chip.h"
#include "model/fx.h"
#include "model/loops.h"

// Sequencer data (PLAN.md §7.1, §6.3): patterns of notes per track, sections (pattern + repeats +
// tempo + signature) and the song order. Positions are in ticks at 960 PPQ.
namespace phos::model {

inline constexpr int kPpq = 960;
inline constexpr int kTracks = 16;
inline constexpr int kMaxPatterns = 64;
inline constexpr int kSections = 26; // A..Z
inline constexpr int kMaxSongSteps = 128;

// Grid (step size). Triplet grids put three steps where two straight ones would be.
enum class Grid : std::uint8_t { Quarter, Eighth, Sixteenth, ThirtySecond, SixtyFourth, EighthT, SixteenthT, ThirtySecondT };
int grid_ticks(Grid g);
const char* grid_name(Grid g); // "1/16", "1/16T"

struct Note {
    std::uint32_t tick = 0;   // start, from the pattern start
    std::uint32_t length = 0; // ticks
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
    friend bool operator==(const Note&, const Note&) = default;
};

// CC automation (PLAN.md §7.7, M8): one controller of one track, a value per step (-1 = no
// change at that step). The value is sent when its step plays and holds until the next one.
struct AutomationLane {
    int track = 0;
    int cc = 7;                       // 0..127
    std::vector<std::int16_t> values{}; // per step, -1 or 0..127; resized with the pattern
    friend bool operator==(const AutomationLane&, const AutomationLane&) = default;
};

struct Pattern {
    std::string name;
    int steps = 16;               // 1..256
    Grid grid = Grid::Sixteenth;
    std::array<std::vector<Note>, kTracks> lanes{}; // sorted by tick
    std::vector<AutomationLane> automation{};       // M8: at most one lane per (track, cc)

    // The lane for (track, cc), or nullptr / a new empty one.
    const AutomationLane* lane(int track, int cc) const;
    AutomationLane& lane_for(int track, int cc);

    std::uint32_t length_ticks() const { return static_cast<std::uint32_t>(steps * grid_ticks(grid)); }
    std::uint32_t step_ticks() const { return static_cast<std::uint32_t>(grid_ticks(grid)); }
    friend bool operator==(const Pattern&, const Pattern&) = default;
};

struct Section {
    int pattern = 0;   // index into Song::patterns
    int repeats = 1;   // 1..99
    int bpm10 = 1200;  // tempo in tenths of a BPM: 20.0..300.0
    int sig_num = 4;   // 1..16
    int sig_den = 4;   // 2, 4, 8, 16
    friend bool operator==(const Section&, const Section&) = default;
};

// Where a track's notes go (PLAN.md §7.7): the instrument, a MIDI output, or both.
enum class MidiDest : std::uint8_t { Internal, External, Both };
const char* midi_dest_name(MidiDest d); // "INT", "EXT", "BOTH"

// Controllers automation also applies inside PHOSPHOR (all of them go to a MIDI output).
inline constexpr int kCcModWheel = 1;
inline constexpr int kCcVolume = 7;
inline constexpr int kCcPan = 10;
inline constexpr int kCcExpression = 11;

// NOTES lanes play their instrument; LOOPS lanes launch loop slots (note 60 + n = slot n + 1) for as
// long as each note lasts (PLAN.md §7.5).
enum class TrackType : std::uint8_t { Notes, Loops };

struct Track {
    std::string name;
    TrackType type = TrackType::Notes;
    int instrument = 0; // 0..15
    float volume_db = 0.0f;
    float pan = 0.0f;   // -1..1
    bool mute = false;
    bool solo = false;
    bool swing = false; // opt-in to the song swing
    std::array<FxSlot, 2> inserts{}; // M7: two insert effects, in order, before the fader
    float send_a = 0.0f;             // post-fader sends to the returns, 0..1
    float send_b = 0.0f;
    MidiDest dest = MidiDest::Internal; // M8
    std::string out_port;               // MIDI output name (EXTERNAL / BOTH)
    int out_channel = 0;                // 0..15 (default: track 1 -> channel 1 ... H -> 16)
    int program = -1;                   // -1 = none, else 0..127: sent at every pattern start
    friend bool operator==(const Track&, const Track&) = default;
};

inline constexpr int kVaporMax = 100;

enum class MetronomeMode : std::uint8_t { Off, Play, Record }; // Record = only while recording
enum class PlayMode : std::uint8_t { Pattern, Song }; // loop the current section, or play the song order

struct Song {
    std::vector<Pattern> patterns{Pattern{"PATTERN 01"}};
    std::array<Section, kSections> sections{};
    std::vector<int> order{0};           // song steps: section indices
    std::array<Track, kTracks> tracks{};
    int swing = 50;                      // 50..75 %
    Grid swing_grid = Grid::Sixteenth;   // Sixteenth or Eighth pairs
    MetronomeMode metronome = MetronomeMode::Record;
    int count_in_bars = 1;               // 0..2
    PlayMode play_mode = PlayMode::Pattern;
    Grid record_quantize = Grid::Sixteenth;
    bool quantize_on_record = false;
    int record_offset_ms = 0;            // latency compensation, -200..200
    float master_db = 0.0f;
    int current_section = 0;             // section edited on Page R and played in PATTERN mode
    int current_track = 0;
    SnesEcho snes_echo;                  // M6: the S-DSP echo and noise clock
    bool strict_cmi = false;             // STRICT modes (PLAN.md §7.4): 16 wavetable voices,
    bool strict_gb = false;              // GB 4 channels (2 pulse, wave, noise, mono each),
    bool strict_snes = false;            // SNES 8 voices
    // M7 (PLAN.md §7.5, §7.8): loop slots, send returns A / B (effect + level), master inserts and
    // the VAPOR macro (0..100).
    std::array<LoopSlot, kLoopSlots> loops{};
    std::array<FxSlot, 2> returns{};
    std::array<float, 2> return_level{1.0f, 1.0f};
    std::array<FxSlot, 2> master_fx{};
    int vapor = 0;

    Song();
    const Section& section() const { return sections[static_cast<std::size_t>(current_section)]; }
    Pattern& pattern() { return patterns[static_cast<std::size_t>(section().pattern)]; }
    const Pattern& pattern() const { return patterns[static_cast<std::size_t>(section().pattern)]; }
    friend bool operator==(const Song&, const Song&) = default;
};

// Inserts keeping the lane sorted; replaces a note with the same start and pitch.
void insert_note(std::vector<Note>& lane, const Note& n);
// Removes notes that start in [from, to) (all pitches). Returns how many.
int remove_notes(std::vector<Note>& lane, std::uint32_t from, std::uint32_t to);

// Track label as on the reference: 1..8, A..H.
char track_label(int track);

} // namespace phos::model
