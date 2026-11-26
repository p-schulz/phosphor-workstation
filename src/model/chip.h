#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// Chip instruments (PLAN.md §7.4, Appendix A). Constants come from the hardware documentation:
// Pan Docs (gbdev.io/pandocs, "Audio Registers", "Audio Details") for the Game Boy APU and fullsnes
// (problemkaputt.de/fullsnes, "SNES APU DSP ...") for the SNES S-DSP. No emulator code is used.
namespace phos::model {

// ---------------------------------------------------------------------------------------------
// Game Boy
// ---------------------------------------------------------------------------------------------
enum class GbChannel : std::uint8_t { Pulse, Wave, Noise };

// A 60 Hz macro (sound-driver style): one value per tick; after the last step it repeats from
// `loop` (if >= 0). While the note is held, playback does not pass `release` (if >= 0); key-off
// jumps there and plays on to the end.
struct GbMacro {
    std::vector<std::int8_t> steps;
    int loop = -1;
    int release = -1;
    friend bool operator==(const GbMacro&, const GbMacro&) = default;
};
enum GbMacroKind : int { kMacroVolume, kMacroDuty, kMacroArp, kMacroPitch, kMacroWave, kMacroCount };

struct GbParams {
    GbChannel channel = GbChannel::Pulse;
    int duty = 2;              // 0..3 = 12.5 / 25 / 50 / 75 %
    int sweep_pace = 0;        // 0 = off, 1..7 x 1/128 s
    bool sweep_down = false;
    int sweep_shift = 0;       // 0..7
    int env_volume = 15;       // 0..15
    bool env_up = false;
    int env_pace = 0;          // 0 = hold, 1..7 x 1/64 s per step
    int length = 0;            // 0 = off, 1..64 x 1/256 s (wave: 1..256)
    int wave_level = 1;        // 0 mute, 1 = 100 %, 2 = 50 %, 3 = 25 %
    std::array<std::array<std::uint8_t, 32>, 8> waves{}; // 4-bit wave RAM shapes (wave macro picks one)
    bool noise_short = false;  // 7-bit LFSR
    bool quantize = true;      // pitch from the 11-bit period register (authentic tuning error)
    bool raw = false;          // naive steps (aliasing) instead of band-limited steps
    int vibrato_cents = 0;
    int vibrato_speed = 6;     // ticks per vibrato cycle / 4
    int vibrato_delay = 0;     // ticks
    int porta_ticks = 0;       // glide from the previous note
    std::array<GbMacro, kMacroCount> macros{};
    friend bool operator==(const GbParams&, const GbParams&) = default;
};
GbParams default_gb(GbChannel channel);

// Frequencies from the period registers (Pan Docs, "Audio Registers").
double gb_pulse_hz(int period);             // 131072 / (2048 - period)
double gb_wave_hz(int period);              // 65536 / (2048 - period), one 32-sample cycle
double gb_noise_hz(int divider, int shift); // 262144 / (divider * 2^shift), divider 0 = 0.5
int gb_pulse_period(double hz);             // nearest 11-bit period
int gb_wave_period(double hz);

// LFSR as in Pan Docs "Noise channel (CH4)": bit 15 = bit0 XNOR bit1 (also bit 7 in short mode),
// then shift right; output = bit 0. Returns the new register.
constexpr std::uint16_t gb_lfsr_step(std::uint16_t lfsr, bool short_mode) {
    const std::uint16_t bit = static_cast<std::uint16_t>(~(lfsr ^ (lfsr >> 1)) & 1u);
    lfsr = static_cast<std::uint16_t>((lfsr & 0x7FFFu) | (bit << 15));
    if (short_mode) {
        lfsr = static_cast<std::uint16_t>((lfsr & ~0x80u) | (bit << 7));
    }
    return static_cast<std::uint16_t>(lfsr >> 1);
}

// ---------------------------------------------------------------------------------------------
// SNES
// ---------------------------------------------------------------------------------------------
// BRR sample: 9-byte blocks of 16 samples (fullsnes "SNES APU DSP BRR Samples").
struct BrrSample {
    std::vector<std::uint8_t> data;
    int loop_block = -1;     // block to jump to at the end, -1 = stop (end+mute)
    int source_rate = 32000;
    std::size_t blocks() const { return data.size() / 9; }
    std::size_t frames() const { return blocks() * 16; }
    friend bool operator==(const BrrSample&, const BrrSample&) = default;
};
using BrrRef = std::shared_ptr<const BrrSample>;

// Encodes 16-bit PCM (values -32768..32767) into BRR. Each block tries every shift 0..12 and
// filter 0..3 against the running decoder state and keeps the smallest squared error. The first
// block and the loop-start block use filter 0 (fullsnes "BRR Notes"). `loop_start` is snapped
// down to a block; the length is padded to whole blocks with silence.
BrrSample brr_encode(const std::vector<std::int16_t>& pcm, int loop_start, int source_rate);
// Decodes to 15-bit samples (as the DSP sees them), following the documented filter equations
// and clipping exactly. Loops are not followed (one pass).
std::vector<std::int16_t> brr_decode(const BrrSample& s);
// One block: `old`/`older` carry the decoder state.
void brr_decode_block(const std::uint8_t* block, std::int16_t out[16], int& old, int& older);

// 512-entry Gaussian interpolation table (fullsnes "SNES APU DSP BRR Pitch").
extern const std::array<std::int16_t, 512> kSnesGauss;
// Envelope / noise rates: 32000 Hz samples per step for 5-bit rates 0..31 (fullsnes
// "ADSR/Gain (and Noise) Rates"); entry 0 = stopped (0).
extern const std::array<std::uint16_t, 32> kSnesRates;

enum class SnesGainMode : std::uint8_t { Direct, LinearDecrease, ExpDecrease, LinearIncrease, BentIncrease };

struct SnesParams {
    bool adsr = true;
    int attack = 15;       // 0..15
    int decay = 7;         // 0..7
    int sustain_level = 7; // 0..7, boundary (SL+1) * 0x100
    int sustain_rate = 0;  // 0..31
    SnesGainMode gain_mode = SnesGainMode::Direct;
    int gain = 127;        // direct: 0..127 (x16); other modes: rate 0..31
    int vol_l = 100;       // -128..127
    int vol_r = 100;
    bool noise = false;    // output the shared noise generator instead of BRR
    bool pitch_mod = false; // modulate by the previous SNES voice
    bool echo = true;      // EON
    int root_note = 60;
    bool preview_original = false; // A/B: play the original sample instead of the BRR copy
    int source_rate = 32000;  // BRR sample rate, 8000..32000 (changing it re-encodes)
    bool auto_align = true;   // resample the loop to a whole number of 16-sample blocks
    friend bool operator==(const SnesParams&, const SnesParams&) = default;
};

// Song-wide S-DSP settings: echo and the noise clock (one each on the chip).
struct SnesEcho {
    int delay = 4;            // EDL 0..15 (x 16 ms)
    int feedback = 50;        // EFB -128..127
    int vol_l = 40;           // EVOL -128..127
    int vol_r = 40;
    std::array<int, 8> fir{127, 0, 0, 0, 0, 0, 0, 0}; // FIR0 (oldest) .. FIR7 (newest), -128..127
    int noise_rate = 0x1C;    // FLG noise clock, 5-bit rate
    friend bool operator==(const SnesEcho&, const SnesEcho&) = default;
};
inline constexpr std::array<std::array<int, 8>, 4> kFirPresets = {{
    {127, 0, 0, 0, 0, 0, 0, 0},           // flat (no filtering)
    {-1, 8, 23, 36, 36, 23, 8, -1},       // low-pass (fullsnes "Filter Examples")
    {52, 51, 0, 0, 0, 0, 0, 0},           // soft
    {64, -32, 16, -8, 4, -2, 1, 0},       // bright ringing
}};

inline constexpr std::size_t kAramBytes = 64 * 1024;
inline constexpr std::size_t kAramDriverReserve = 8 * 1024; // assumed sound-driver code + tables
inline constexpr std::size_t snes_echo_bytes(int edl) { return edl == 0 ? 4 : static_cast<std::size_t>(edl) * 2048; }

} // namespace phos::model
