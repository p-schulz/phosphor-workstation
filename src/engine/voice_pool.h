#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <span>

#include "engine/drum_voice.h"
#include "engine/gb_voice.h"
#include "engine/snes_voice.h"
#include "engine/messages.h"
#include "engine/sampler_voice.h"
#include "engine/synth_voice.h"
#include "engine/wave_voice.h"

namespace phos::engine {

// Fixed voice pool: one tagged slot per voice for every instrument kind (SAMPLER, WAVETABLE, SYNTH,
// DRUM) plus the M1 test voices (sine, click). No allocation after construction.
// Steal order (PLAN.md §6.5): released-oldest -> quietest -> oldest.
class VoicePool {
public:
    static constexpr int kMaxVoices = 64;

    void prepare(float sample_rate) noexcept;
    void set_limit(int voices) noexcept;
    void set_envelope(float attack_ms, float release_ms) noexcept;

    // `bus`: track 0..15, or kDirectBus (metronome, audition).
    void note_on(TestWave wave, int note, float velocity, int bus = 0) noexcept;
    void note_on_sampler(int slot, const SamplerBlock& block, int note, float velocity, int bus = 0) noexcept;
    // Any instrument kind. `blocks` gives the other instruments too (drum pads that play a sample).
    void note_on_instrument(int slot, std::span<const SamplerBlock> blocks, int note, float velocity, int bus) noexcept;
    // Releases the sampler voices of one instrument slot (audition stop).
    void release_slot(int slot) noexcept;
    // Releases voices playing `note` (on `bus` only, if >= 0). Live notes honour the sustain pedal,
    // sequencer note-offs do not.
    void note_off(int note, int bus = -1, bool honour_sustain = true) noexcept;
    // Sustain pedal (CC64): while down, note-offs are deferred until the pedal goes up.
    void set_sustain(bool down) noexcept;
    // Pitch multiplier for all sine voices (pitch bend), and mod-wheel vibrato depth 0..1
    // (5.5 Hz, up to +-50 cents).
    void set_pitch_ratio(double ratio) noexcept { pitch_ratio_ = ratio; }
    void set_vibrato(float depth) noexcept { vibrato_depth_ = depth; }
    void release_all() noexcept;
    void kill_all() noexcept;

    // Adds each voice into its bus (lefts[bus], rights[bus], kBusCount entries). Sampler voices
    // read their live parameters from `blocks`.
    void render(std::span<float* const> lefts, std::span<float* const> rights, std::size_t frames,
                std::span<const SamplerBlock> blocks, double bpm = 120.0) noexcept;
    // Everything into one stereo buffer (tests).
    void render(float* left, float* right, std::size_t frames, std::span<const SamplerBlock> blocks = {}) noexcept;
    // SNES voices at 32 kHz into the SNES bus scratch (dry and echo send), sample by sample so that
    // pitch modulation can read the previous voice. `track_gain` / `track_pan`: per bus.
    void render_snes(float* dry_l, float* dry_r, float* wet_l, float* wet_r, std::size_t frames,
                     std::span<const SamplerBlock> blocks, const std::int16_t* noise,
                     std::span<const float> track_gain, std::span<const float> track_pan) noexcept;
    // STRICT modes (PLAN.md §7.4): CMI 16 wavetable voices, GB 4 channels, SNES 8 voices.
    void set_strict(bool cmi, bool gb, bool snes) noexcept {
        strict_cmi_ = cmi;
        strict_gb_ = gb;
        strict_snes_ = snes;
    }
    int active_of(model::InstrumentKind kind) const noexcept;

    // Calls fn(const void*) with the sample data or wave tables every sounding voice reads; stops
    // when fn returns false (the engine keeps those alive until no voice uses them).
    template <typename Fn> bool all_resources(Fn&& fn) const noexcept {
        for (const Voice& v : voices_) {
            if (v.stage == Stage::Off) {
                continue;
            }
            const void* r = v.kind == Kind::Sampler ? static_cast<const void*>(v.sv.sample())
                            : v.kind == Kind::Wave  ? static_cast<const void*>(v.wv.tables())
                            : v.kind == Kind::Snes  ? static_cast<const void*>(v.snes.brr())
                                                    : nullptr;
            if (r && !fn(r)) {
                return false;
            }
        }
        return true;
    }

    int active() const noexcept;
    int limit() const noexcept { return limit_; }
    std::uint64_t steals() const noexcept { return steals_; }

    static double note_hz(int note) noexcept; // A4 (69) = 440 Hz

private:
    enum class Stage : std::uint8_t { Off, Attack, Sustain, Release };
    enum class Kind : std::uint8_t { Test, Sampler, Wave, Synth, Drum, Gb, Snes };
    struct Voice {
        Stage stage = Stage::Off;
        TestWave wave = TestWave::Sine;
        int note = 0;
        std::uint64_t order = 0; // start order, smaller = older
        double phase = 0.0;
        double phase_inc = 0.0;
        float velocity = 0.0f;
        float env = 0.0f;
        std::uint32_t age = 0; // frames since start (click length)
        bool sustained = false; // note-off arrived while the pedal was down
        Kind kind = Kind::Test;
        std::uint8_t bus = 0;
        int slot = 0; // instrument slot (wave, synth, drum)
        SamplerVoice sv;
        WaveVoice wv;
        SynthVoice syn;
        DrumVoice dv;
        GbVoice gb;
        SnesVoice snes;
        int gb_channel = -1; // STRICT GB: hardware channel 0..3
    };

    int pick_slot() noexcept;
    void render_voice(Voice& v, float* left, float* right, std::size_t frames, double pitch) noexcept;
    void release(Voice& v) noexcept;
    float level(const Voice& v) const noexcept {
        switch (v.kind) {
        case Kind::Sampler:
            return v.sv.level();
        case Kind::Wave:
            return v.wv.level();
        case Kind::Synth:
            return v.syn.level();
        case Kind::Drum:
            return v.dv.level();
        case Kind::Gb:
            return v.gb.level();
        case Kind::Snes:
            return v.snes.level();
        case Kind::Test:
            break;
        }
        return v.env * v.velocity;
    }
    void limit_kind(Kind kind, int max) noexcept;     // kill the oldest of `kind` until fewer than max
    int gb_channel_for(model::GbChannel c) noexcept;  // STRICT GB channel, freeing it
    Voice& start_voice(Kind kind, int slot, int note, float velocity, int bus) noexcept;

    std::array<Voice, kMaxVoices> voices_{};
    int limit_ = 32;
    float sample_rate_ = 48000.0f;
    float attack_step_ = 0.0f;
    float release_coef_ = 0.0f;
    std::uint64_t next_order_ = 1;
    std::uint64_t steals_ = 0;
    bool sustain_ = false;
    double pitch_ratio_ = 1.0;
    float vibrato_depth_ = 0.0f;
    double lfo_phase_ = 0.0;
    std::array<int, kInstrumentSlots> last_note_{}; // per instrument, for glide
    std::uint32_t seed_ = 0x1234567u;
    bool strict_cmi_ = false;
    bool strict_gb_ = false;
    bool strict_snes_ = false;
};

} // namespace phos::engine
