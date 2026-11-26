#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/loop_player.h"
#include "model/instrument.h"
#include "model/sample_data.h"
#include "seq/compiled_song.h"

namespace phos::dsp {
class FxUnit;
}

namespace phos::engine {

// Test voices of M1. Real instrument kinds arrive from M3 on (PLAN.md §6.5).
enum class TestWave : std::uint8_t { Sine, Click };

enum class EngineParam : std::uint8_t { None, MasterGainDb };

// MIDI event placement (PLAN.md §6.4).
enum class Placement : std::uint8_t {
    Immediate, // render at the start of the block that drains the event: latency <= 1 block
    Stable,    // render at timestamp + 1 block: constant latency, no jitter
};

enum class VelocityCurve : std::uint8_t { Linear, Soft, Hard, Fixed };

inline constexpr int kMaxMidiSlots = 8;
inline constexpr int kAuditionSlot = 16;    // after the 16 instruments: Page 2 audition
inline constexpr int kInstrumentSlots = 17;
inline constexpr std::uint8_t kKeyjazzPort = 0xFF;   // computer keyboard events (not a MIDI slot)
inline constexpr std::uint8_t kSequencerPort = 0xFE; // notes from the song
inline constexpr std::uint8_t kMetronomePort = 0xFD; // metronome clicks
inline constexpr int kTrackCount = 16;
inline constexpr int kDirectBus = 16; // metronome and audition: straight to the master, not a track
inline constexpr int kBusCount = 17;

// MIDI out (PLAN.md §7.7): up to 8 open outputs ("slots"). The audio thread queues messages with
// the engine frame they belong to; the MIDI-out thread sends them at that frame's host time.
inline constexpr int kMaxMidiOutSlots = 8;
struct MidiOutEvent {
    std::uint64_t frame = 0;
    std::uint8_t slot = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 3> bytes{};
};

enum class TransportAction : std::uint8_t { Play, PlayRecord, Stop, Continue, ToggleRecord };

// UI -> audio thread message (trivially copyable, carried by SpscRing).
struct EngineCommand {
    enum class Type : std::uint8_t { NoteOn, NoteOff, AllNotesOff, Param, Midi, SamplerNoteOn, SamplerRelease, Transport, Panic, Loop };
    Type type = Type::NoteOn;
    TestWave wave = TestWave::Sine;
    std::int16_t note = 60;
    float value = 0.0f; // velocity 0..1 for NoteOn, parameter value for Param
    EngineParam param = EngineParam::None;
    std::array<std::uint8_t, 3> midi{}; // Type::Midi: status, data1, data2
    std::int64_t ns = 0;                // Type::Midi: host timestamp (steady_clock), 0 = now
    std::int8_t slot = 0;               // SamplerNoteOn / SamplerRelease: instrument slot

    static EngineCommand note_on(TestWave wave, int note, float velocity) {
        EngineCommand c;
        c.type = Type::NoteOn;
        c.wave = wave;
        c.note = static_cast<std::int16_t>(note);
        c.value = velocity;
        return c;
    }
    static EngineCommand note_off(int note) {
        EngineCommand c;
        c.type = Type::NoteOff;
        c.note = static_cast<std::int16_t>(note);
        return c;
    }
    static EngineCommand all_notes_off() {
        EngineCommand c;
        c.type = Type::AllNotesOff;
        return c;
    }
    static EngineCommand set_param(EngineParam p, float v) {
        EngineCommand c;
        c.type = Type::Param;
        c.param = p;
        c.value = v;
        return c;
    }
    // Plays instrument `slot` directly (audition), independent of the current instrument.
    static EngineCommand sampler_note_on(int slot, int note, float velocity) {
        EngineCommand c;
        c.type = Type::SamplerNoteOn;
        c.slot = static_cast<std::int8_t>(slot);
        c.note = static_cast<std::int16_t>(note);
        c.value = velocity;
        return c;
    }
    static EngineCommand sampler_release(int slot) {
        EngineCommand c;
        c.type = Type::SamplerRelease;
        c.slot = static_cast<std::int8_t>(slot);
        return c;
    }
    // PANIC: silence every voice now, sustain pedal up, bend/mod reset, held notes forgotten.
    static EngineCommand panic() {
        EngineCommand c;
        c.type = Type::Panic;
        return c;
    }
    // Page L: launch (action 1), stop (0) or toggle (2) loop slot `slot`, quantised by the slot.
    static EngineCommand loop(int slot, int action) {
        EngineCommand c;
        c.type = Type::Loop;
        c.slot = static_cast<std::int8_t>(slot);
        c.note = static_cast<std::int16_t>(action);
        return c;
    }
    static EngineCommand transport(TransportAction a) {
        EngineCommand c;
        c.type = Type::Transport;
        c.note = static_cast<std::int16_t>(a);
        return c;
    }
    // A MIDI message from a non-MIDI source (KEYJAZZ), handled exactly like MIDI input.
    static EngineCommand midi_message(std::uint8_t status, std::uint8_t d1, std::uint8_t d2, std::int64_t ns) {
        EngineCommand c;
        c.type = Type::Midi;
        c.midi = {status, d1, d2};
        c.ns = ns;
        return c;
    }
};

struct WaveTables;

// One instrument as the audio thread sees it: raw pointers to its data + parameters. The name is
// historical (M3); it carries every instrument kind.
struct SamplerBlock {
    const model::SampleData* sample = nullptr;
    model::SamplerParams params;
    model::InstrumentKind kind = model::InstrumentKind::Sampler;
    const WaveTables* tables = nullptr;
    model::WavetableParams wave;
    model::SynthParams synth;
    model::DrumKit drum{};
    model::GbParams gb;
    model::SnesParams snes;
    const model::BrrSample* brr = nullptr;

    // Something to play (else live notes fall back to the test sine).
    bool playable() const {
        switch (kind) {
        case model::InstrumentKind::Sampler:
            return sample != nullptr;
        case model::InstrumentKind::Wavetable:
            return tables != nullptr;
        case model::InstrumentKind::Snes:
            return brr != nullptr || (snes.preview_original && sample != nullptr);
        default:
            return true;
        }
    }
};

inline SamplerBlock sampler_block(const model::SampleData* sample, const model::SamplerParams& params) {
    SamplerBlock b;
    b.sample = sample;
    b.params = params;
    return b;
}

// A MIDI CC bound to an engine parameter (CC learn). channel 0xFF = any channel.
struct CcBinding {
    std::uint8_t cc = 0;
    std::uint8_t channel = 0xFF;
    EngineParam param = EngineParam::None;
};

// Immutable compiled engine state (PLAN.md §6.3). Built on the UI thread, published by pointer,
// swapped in at a block boundary and handed back through the release queue for the UI to free.
// M1/M2 carry engine-wide settings; the song and instruments join it from M3/M4 on.
struct EngineSnapshot {
    std::uint64_t serial = 0;
    float limiter_ceiling_db = -1.0f;
    float limiter_release_ms = 80.0f;
    float voice_attack_ms = 5.0f;
    float voice_release_ms = 60.0f;
    int voice_limit = 32; // PLAN.md §6.5 default pool size

    Placement placement = Placement::Immediate;
    VelocityCurve velocity_curve = VelocityCurve::Linear;
    int bend_range_semitones = 2;
    // Per MIDI input slot: -1 = all channels (OMNI), 0..15 = only that channel.
    std::array<std::int8_t, kMaxMidiSlots> slot_channel{-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<CcBinding, 32> cc_bindings{};
    int cc_binding_count = 0;

    // Instruments (M3). `sample_refs` keeps the PCM alive for as long as the snapshot exists;
    // snapshots are destroyed on the UI thread only, so no reference count changes on the audio
    // thread. The engine holds a replaced snapshot back while voices still play its samples.
    std::array<SamplerBlock, kInstrumentSlots> instruments{};
    std::array<model::SampleRef, kInstrumentSlots> sample_refs{};
    std::array<std::shared_ptr<const WaveTables>, kInstrumentSlots> table_refs{}; // M5, same rules
    std::array<model::BrrRef, kInstrumentSlots> brr_refs{};                         // M6, same rules
    int current_instrument = 0;

    seq::CompiledSong song; // M4: patterns, sections, tracks (owned vectors, freed on the UI thread)

    // M8 MIDI out: off unless enabled in the settings. Per track the output slot of its port
    // (-1 = none open); outputs that get clock and transport; live input forwarded to the current
    // track's output (THRU).
    bool midi_out = false;
    std::array<std::int8_t, kTrackCount> track_out_slot{-1, -1, -1, -1, -1, -1, -1, -1,
                                                        -1, -1, -1, -1, -1, -1, -1, -1};
    std::uint8_t open_slots = 0;  // bitmask of open outputs (PANIC)
    std::uint8_t clock_slots = 0; // bitmask: clock 24 PPQN, start / continue / stop, SPP
    bool thru = true;

    // M7: loop slots (samples and stretch caches kept alive by loop_refs) and the effect units.
    // A unit keeps its state (delay lines, reverb tail) across snapshots: the builder hands the
    // same unit to the next snapshot until its type or the sample rate changes. Only the audio
    // thread runs a unit; the UI thread frees it with the last snapshot that holds it.
    std::array<LoopBlock, model::kLoopSlots> loops{};
    std::array<model::SampleRef, model::kLoopSlots * 2> loop_refs{};
    struct FxRack {
        std::array<std::array<dsp::FxUnit*, 2>, kTrackCount> tracks{};
        std::array<dsp::FxUnit*, 2> returns{};
        std::array<dsp::FxUnit*, 2> master{};
        std::array<dsp::FxUnit*, 3> vapor{}; // PLATE, LOFI, TAPE driven by the VAPOR amount
    } fx;
    std::vector<std::shared_ptr<dsp::FxUnit>> fx_refs;
};

// Audio -> UI while recording: a live note-on/off at its position in the playing pattern.
struct RecEvent {
    bool on = true;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint8_t track = 0;
    std::int16_t pattern = 0;
    std::uint32_t tick = 0;
};

// Audio -> UI: every received control change (for CC learn and the MIDI monitor).
struct CcEvent {
    std::uint8_t port = 0;
    std::uint8_t channel = 0;
    std::uint8_t cc = 0;
    std::uint8_t value = 0;
};

} // namespace phos::engine
