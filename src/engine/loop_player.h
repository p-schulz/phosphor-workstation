#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

#include "dsp/smoother.h"
#include "dsp/svf.h"
#include "model/loops.h"
#include "model/sample_data.h"

// Page L loop slots on the audio thread (PLAN.md §7.5). Every slot keeps a musical phase in beats
// that advances at the project tempo (times SCREW and the VAPOR slow-down); the sync mode maps that
// phase to a source position:
//   VARISPEED  position = phase / beats * length: rate and pitch follow the tempo.
//   STRETCH    the same on the stretch cache rendered for the current tempo (pitch kept); until the
//              cache for this tempo is ready, the slot plays VARISPEED.
//   SLICE      the phase picks one of SLICES equal slices; each plays from its start at the
//              original speed and is cut (with a short fade) when the next one is due.
// A tempo change therefore reflows every mode at once. No allocation, locks or I/O.
namespace phos::engine {

// One slot as the snapshot carries it (pointers stay valid while the snapshot lives).
struct LoopBlock {
    const model::SampleData* sample = nullptr;
    const model::SampleData* stretched = nullptr; // STRETCH cache, rendered for `stretch_bpm10`
    int stretch_bpm10 = 0;
    double beats = 4.0;
    model::LoopSync sync = model::LoopSync::Varispeed;
    model::LaunchQuantize quantize = model::LaunchQuantize::Bar;
    int slices = 16;
    float gain = 1.0f;
    float lp_hz = 20000.0f;
    bool reverse = false;
    float screw = 1.0f;
    int track = 8;
};

enum class LoopState : std::uint8_t { Off, Pending, Playing };

class LoopPlayer {
public:
    static constexpr int kSlots = model::kLoopSlots;

    void prepare(float sample_rate) noexcept;
    // Starts slot `slot` at engine frame `at` from its first beat (restarts a playing slot).
    void launch(int slot, std::uint64_t at) noexcept;
    // Stops at `at` (short fade). A pending launch is cancelled.
    void stop(int slot, std::uint64_t at) noexcept;
    void stop_all(std::uint64_t at) noexcept;
    void kill_all() noexcept;
    LoopState state(int slot) const noexcept { return slots_[static_cast<std::size_t>(slot)].state; }
    bool active(int slot) const noexcept { return state(slot) != LoopState::Off; }
    bool any_active() const noexcept {
        for (int i = 0; i < kSlots; ++i) {
            if (active(i)) {
                return true;
            }
        }
        return false;
    }

    // Adds [frame, frame + n) of every active slot into its track bus. `bpm10`: current tempo;
    // `speed`: VAPOR slow-down (1 = none).
    void render(std::span<const LoopBlock> blocks, std::span<float* const> lefts, std::span<float* const> rights,
                std::uint64_t frame, std::uint32_t n, int bpm10, float speed) noexcept;

    // For the UI (Page L): state and phase (0..1 of the loop) of each slot, updated per render.
    void publish(std::array<std::atomic<std::uint8_t>, kSlots>& states,
                 std::array<std::atomic<float>, kSlots>& phases) const noexcept;

private:
    struct Slot {
        LoopState state = LoopState::Off;
        std::uint64_t start = UINT64_MAX; // pending launch or restart frame
        std::uint64_t stop_at = UINT64_MAX;
        double phase = 0.0; // beats
        double beats = 4.0;
        float fade = 0.0f;  // launch / stop fade 0..1
        bool stopping = false;
        dsp::OnePoleSmoother gain;
        dsp::Svf lp_l;
        dsp::Svf lp_r;
    };
    float frame_out(const LoopBlock& b, Slot& s, int bpm10, float& r) noexcept;

    float rate_ = 48000.0f;
    std::array<Slot, kSlots> slots_{};
};

} // namespace phos::engine
