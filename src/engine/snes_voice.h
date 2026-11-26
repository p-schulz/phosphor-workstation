#pragma once

#include <cstdint>

#include "model/instrument.h"

namespace phos::engine {

// One S-DSP voice at 32000 Hz (PLAN.md §7.4; fullsnes "SNES APU DSP BRR Samples / BRR Pitch /
// ADSR/Gain Envelope"): BRR decoding with the four prediction filters, the 16-bit pitch counter
// (0x1000 = 1.0), 4-point Gaussian interpolation from the hardware table, the 11-bit ADSR / GAIN
// envelope on the hardware rate table, noise mode and pitch modulation by the previous voice.
class SnesVoice {
public:
    void start(const model::BrrSample* brr, const model::SnesParams& p, int note, float fine_cents, float velocity);
    void release() { stage_ = Stage::Release; }
    void kill() { brr_ = nullptr; }
    bool active() const { return brr_ != nullptr; }
    float level() const { return static_cast<float>(env_) / 2047.0f * velocity_; }
    const model::BrrSample* brr() const { return brr_; }

    // One 32 kHz sample. `prev_out` is the previous voice's output (pitch modulation), `noise` the
    // shared noise generator level (both 15-bit). Returns this voice's 15-bit output after the
    // envelope (VxOUTX before the volumes); `l`/`r` get it times VxVOL/128 and velocity.
    int sample(const model::SnesParams& p, double bend, int prev_out, int noise, float& l, float& r) noexcept;

    // Pitch register for a note (exposed for tests): 0x1000 * rate/32000 * 2^((note-root)/12).
    static int pitch_register(int source_rate, int note, int root, float fine_cents, double bend);

private:
    enum class Stage : std::uint8_t { Attack, Decay, Sustain, Release };
    void next_block() noexcept;
    void envelope(const model::SnesParams& p) noexcept;

    const model::BrrSample* brr_ = nullptr;
    int note_ = 60;
    float fine_ = 0.0f;
    float velocity_ = 1.0f;
    int block_ = 0;
    std::uint32_t counter_ = 0; // bits 15..12: sample in block, 11..4: Gaussian index
    std::int16_t buf_[19] = {}; // 3 history + 16 samples of the current block
    int old_ = 0;
    int older_ = 0;
    bool end_after_block_ = false;
    int env_ = 0;               // 0..0x7FF
    Stage stage_ = Stage::Attack;
    int rate_counter_ = 0;
    int delay_ = 0;             // key-on: 5 silent samples (fullsnes "KON")
    int last_out_ = 0;
};

} // namespace phos::engine
