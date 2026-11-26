#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "model/chip.h"
#include "model/sample_data.h"

namespace phos::model {

enum class Interp : std::uint8_t { Drop, Linear, Hermite };       // PLAN.md §7.3 SAMPLER
enum class LoopMode : std::uint8_t { Off, Forward, PingPong };
enum class FilterType : std::uint8_t { Off, LowPass, HighPass, BandPass };

struct Adsr {
    float attack_ms = 2.0f;
    float decay_ms = 300.0f;
    float sustain = 1.0f; // 0..1
    float release_ms = 200.0f;
    friend bool operator==(const Adsr&, const Adsr&) = default;
};

struct SamplerParams {
    int root_note = 60;        // C4
    float fine_cents = 0.0f;   // -100..100
    float volume_db = 0.0f;    // -60..+12
    float pan = 0.0f;          // -1..1
    Interp interp = Interp::Hermite;
    LoopMode loop = LoopMode::Off;
    std::size_t loop_start = 0; // frames, [start, end)
    std::size_t loop_end = 0;
    Adsr amp;
    FilterType filter = FilterType::Off;
    float cutoff_hz = 8000.0f;
    float resonance = 0.0f;    // 0..1
    float env_amount = 0.0f;   // octaves, -8..8, filter envelope -> cutoff
    float key_track = 0.0f;    // 0..1 (1 = cutoff follows the keyboard)
    Adsr filter_env{2.0f, 300.0f, 0.0f, 200.0f};
    float vel_to_amp = 1.0f;   // 0..1
    float vel_to_cutoff = 0.0f; // octaves at full velocity, 0..4
    friend bool operator==(const SamplerParams&, const SamplerParams&) = default;
};

enum class InstrumentKind : std::uint8_t { Sampler, Wavetable, Synth, Drum, Gb, Snes };
const char* kind_name(InstrumentKind k); // "SAMPLER", "WAVE", "SYNTH", "DRUM", "GB", "SNES"

// WAVETABLE (PLAN.md §7.3): up to 128 segments of one 128-sample cycle each. `harmonics` are the
// 32 harmonic amplitudes per segment (Pages 4 and 0) that COMPUTE turns into `wave`; Page 6 draws
// `wave` directly. Immutable like SampleData: edits make a new one.
struct WaveSegment {
    std::array<float, 128> wave{};
    std::array<float, 32> harmonics{};
    int cycles = 4; // duration in cycles of the note, 1..255
    friend bool operator==(const WaveSegment&, const WaveSegment&) = default;
};
struct WavetableData {
    static constexpr int kMaxSegments = 128;
    std::vector<WaveSegment> segments;
    friend bool operator==(const WavetableData&, const WavetableData&) = default;
};
using WavetableRef = std::shared_ptr<const WavetableData>;

struct WavetableParams {
    int loop_start = 0;  // segments
    int loop_length = 0; // 0 = no loop: stays on the last segment
    bool crossfade = true;
    bool authentic = false; // no band limiting (aliasing like the 1980s hardware)
    friend bool operator==(const WavetableParams&, const WavetableParams&) = default;
};

enum class OscWave : std::uint8_t { Saw, Pulse, Triangle, Sine, Noise, Off };
enum class LfoShape : std::uint8_t { Sine, Triangle, Square, SampleHold };

struct SynthParams {
    OscWave osc1 = OscWave::Saw;
    OscWave osc2 = OscWave::Pulse;
    int osc2_semitones = 0;    // -24..24
    float osc2_detune = 7.0f;  // cents
    float osc1_level = 1.0f;
    float osc2_level = 0.6f;
    float pulse_width = 0.5f;  // 0.05..0.95
    float sub_level = 0.0f;    // square one octave down
    float noise_level = 0.0f;
    int unison = 1;            // 1..4
    float unison_spread = 12.0f; // cents between unison copies, also spread in the stereo field
    LfoShape lfo_shape = LfoShape::Sine;
    float lfo_rate_hz = 5.0f;
    int lfo_sync = 0;          // 0 = free; else steps per LFO cycle at 1/16 (4 = one beat)
    float lfo_to_pitch = 0.0f;  // cents
    float lfo_to_cutoff = 0.0f; // octaves
    float lfo_to_pw = 0.0f;     // 0..0.45
    float lfo_to_amp = 0.0f;    // 0..1 (tremolo)
    float glide_ms = 0.0f;
    bool mono = false;          // legato: a held mono voice glides without retriggering
    friend bool operator==(const SynthParams&, const SynthParams&) = default;
};

enum class DrumType : std::uint8_t { Off, Kick, Snare, Hat, Clap, Chip, Tom, Sample };
const char* drum_type_name(DrumType t);

struct DrumPad {
    DrumType type = DrumType::Off;
    float tune = 0.0f;       // semitones
    float decay_ms = 200.0f;
    float tone = 0.5f;       // type-specific: kick click, snare noise mix, hat brightness, clap tail, chip rate
    float level_db = 0.0f;
    float pan = 0.0f;
    int choke = 0;           // 0 = none, 1..4: a pad cuts the others in its group (closed / open hat)
    int sample_slot = -1;    // Sample pads: instrument (0..15) played at its root key
    friend bool operator==(const DrumPad&, const DrumPad&) = default;
};
// Pad n answers MIDI note 36 + n (GM: 36 kick, 38 snare, 42 closed hat, 46 open hat).
inline constexpr int kDrumPads = 16;
inline constexpr int kDrumBaseNote = 36;
using DrumKit = std::array<DrumPad, kDrumPads>;
DrumKit default_kit();

// A sound definition (PLAN.md §6.5). `sampler` holds the parameters every kind shares (level, pan,
// fine tune, amp and filter envelopes, filter, velocity); each kind adds its own.
struct Instrument {
    std::string name = "EMPTY";
    InstrumentKind kind = InstrumentKind::Sampler;
    SampleRef sample;
    SamplerParams sampler;
    WavetableRef wavetable;
    WavetableParams wave;
    SynthParams synth;
    DrumKit drum{};
    GbParams gb;
    SnesParams snes;
    BrrRef brr; // SNES: the BRR copy of `sample` at snes source rate
    // Nothing to play: a SAMPLER without a sample or a WAVETABLE without segments. SYNTH and DRUM
    // always play.
    bool empty() const {
        return kind == InstrumentKind::Sampler     ? !sample
               : kind == InstrumentKind::Wavetable ? !wavetable || wavetable->segments.empty()
               : kind == InstrumentKind::Snes      ? !brr
                                                   : false;
    }
    friend bool operator==(const Instrument&, const Instrument&) = default;
};

// Fresh instrument of a kind with useful defaults (a bright wavetable, a saw pad, a GM kit).
Instrument make_instrument(InstrumentKind kind, std::string name);

} // namespace phos::model
