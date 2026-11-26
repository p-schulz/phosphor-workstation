#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "dsp/blep.h"
#include "model/instrument.h"

namespace phos::engine {

// One Game Boy-style note (PLAN.md §7.4, Pan Docs "Audio Registers" / "Audio Details"): a pulse
// (duty 12.5-75 %, optional period sweep), wave (32 x 4-bit) or noise (15/7-bit LFSR) channel,
// the 4-bit volume envelope (64 Hz), length timer (256 Hz), and sound-driver macros at 60 Hz
// (volume, duty, arpeggio, pitch, wave), vibrato and portamento. Pitch can be quantised to the
// 11-bit period register. Rendered at the device rate with band-limited steps, then the DMG
// output high-pass (charge factor 0.999958 per 4.194304 MHz clock).
class GbVoice {
public:
    void start(const model::GbParams& p, int note, int from_note, float rate);
    void release() { release_pending_ = true; } // applied at the next render (needs the macros)
    void kill() { active_ = false; }
    bool active() const { return active_; }
    float level() const { return stopped_ ? 0.0f : static_cast<float>(volume_) / 15.0f; }
    model::GbChannel channel() const { return channel_; }

    // Adds the mono output (times `gain`) into left/right with `pl`/`pr` pan gains.
    void render(float* left, float* right, std::size_t frames, const model::GbParams& p, double pitch, float gain,
                float pl, float pr) noexcept;

private:
    void tick60(const model::GbParams& p) noexcept;   // macros, vibrato, portamento
    void tick_frame_seq(const model::GbParams& p, int step) noexcept; // 512 Hz frame sequencer
    double target_hz(const model::GbParams& p, double pitch) const noexcept;
    int digital(const model::GbParams& p) const noexcept; // 0..15 at the current position
    int macro_value(const model::GbParams& p, int kind, int fallback) const noexcept;

    bool active_ = false;
    bool released_ = false;
    bool release_pending_ = false;
    bool stopped_ = false; // output gated off for good (key-off done, length / sweep end); macros cannot reopen it
    void apply_release(const model::GbParams& p);
    model::GbChannel channel_ = model::GbChannel::Pulse;
    float rate_ = 48000.0f;
    int note_ = 60;
    double porta_from_ = -1.0; // semitones; glides to note_
    int porta_ticks_left_ = 0;
    double phase_ = 0.0;      // cycles (pulse: 1 cycle = 8 duty steps; wave: 32 samples)
    double noise_acc_ = 0.0;
    std::uint16_t lfsr_ = 0;
    int volume_ = 15;         // 0..15 after envelope
    int env_timer_ = 0;
    int sweep_timer_ = 0;
    int sweep_period_ = -1;   // current swept period, -1 = from the note
    int length_left_ = 0;     // 256 Hz ticks, 0 = off
    double seq_acc_ = 0.0;    // 512 Hz frame sequencer
    int seq_step_ = 0;
    double tick_acc_ = 0.0;   // 60 Hz driver tick
    int ticks_ = 0;
    std::array<int, model::kMacroCount> macro_pos_{};
    int last_digital_ = 0;
    float last_analog_ = 0.0f;
    float hp_cap_ = 0.0f;
    float hp_factor_ = 0.999f;
    dsp::BlepBuffer blep_;
    int silent_frames_ = 0;
    double hz_ = 440.0;         // current pitch, updated per render call and per tick
    double noise_target_ = -1.0; // cached nearest LFSR clock for hz_
    double noise_clock_ = 0.0;
};

} // namespace phos::engine
