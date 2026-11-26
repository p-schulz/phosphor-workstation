#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "dsp/stream_resampler.h"
#include "model/chip.h"

namespace phos::engine {

// The S-DSP output stage at 32000 Hz (PLAN.md §6.5, §7.4): mixes the SNES voices (dry + echo
// send), runs the shared noise generator and the echo (fullsnes "SNES APU DSP Echo Registers":
// EDL x 16 ms ring buffer, 8-tap FIR with signed 8-bit coefficients, EFB feedback, EVOL output),
// then resamples to the device rate. The voices themselves are rendered by the voice pool through
// the callbacks of process().
class SnesBus {
public:
    static constexpr std::size_t kMaxEcho = 15 * 512; // EDL 15 = 240 ms at 32 kHz
    static constexpr std::size_t kScratch = 4096;

    void prepare(double device_rate) {
        resampler_.prepare(32000.0, device_rate);
        reset();
    }
    void reset();

    // Frames at 32 kHz needed for `device_frames` more output frames.
    std::size_t needed(std::size_t device_frames) const {
        return std::min(resampler_.needed(device_frames), kScratch);
    }
    // Shared noise generator (fullsnes "NON"/"FLG"): `n` levels at the FLG noise rate.
    const std::int16_t* noise(std::size_t n, int rate) noexcept;

    // Scratch the voices render into (32 kHz): dry and echo-send, left/right.
    float* dry_l() { return dry_l_.data(); }
    float* dry_r() { return dry_r_.data(); }
    float* wet_l() { return wet_l_.data(); }
    float* wet_r() { return wet_r_.data(); }

    // After the voices: echo on the `n` rendered frames, resample, add to the device buffers.
    void finish(std::size_t n, const model::SnesEcho& echo, float* out_l, float* out_r, std::size_t device_frames) noexcept;

    // Echo one stereo frame (exposed for the FIR impulse test).
    void echo_frame(float in_l, float in_r, const model::SnesEcho& e, float& out_l, float& out_r) noexcept;

private:
    dsp::StreamResampler resampler_;
    std::vector<float> echo_l_ = std::vector<float>(kMaxEcho);
    std::vector<float> echo_r_ = std::vector<float>(kMaxEcho);
    std::size_t echo_pos_ = 0;
    std::array<float, 8> fir_l_{};
    std::array<float, 8> fir_r_{};
    std::size_t fir_pos_ = 0;
    std::vector<float> dry_l_ = std::vector<float>(kScratch);
    std::vector<float> dry_r_ = std::vector<float>(kScratch);
    std::vector<float> wet_l_ = std::vector<float>(kScratch);
    std::vector<float> wet_r_ = std::vector<float>(kScratch);
    std::vector<std::int16_t> noise_ = std::vector<std::int16_t>(kScratch);
    int noise_level_ = -0x4000; // "initially -4000h after reset"
    int noise_counter_ = 0;
};

} // namespace phos::engine
