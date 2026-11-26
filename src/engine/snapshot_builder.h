#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "engine/messages.h"
#include "model/workspace.h"

// Builds the document part of an engine snapshot (PLAN.md §6.3) on the UI or a worker thread:
// instruments (with cached band-limited wavetables), the compiled song, loop slots with their
// STRETCH caches, and the effect units. Effect units and wave tables are cached between builds so
// an edit that does not touch them keeps their state (reverb tails, delay lines) alive.
namespace phos::engine {

struct WaveTables;

// What a STRETCH cache was rendered from.
struct StretchKey {
    const model::SampleData* source = nullptr;
    int bpm10 = 0;
    float shift = 0.0f;
    double beats = 0.0;
    friend bool operator==(const StretchKey&, const StretchKey&) = default;
};

// Renders the STRETCH cache for `key` at `sample_rate` (worker thread; allocates).
model::SampleRef render_stretch(const model::SampleData& source, const StretchKey& key, std::uint32_t sample_rate);

class SnapshotBuilder {
public:
    SnapshotBuilder();
    ~SnapshotBuilder();

    std::unique_ptr<EngineSnapshot> build(const model::Document& doc, std::uint32_t sample_rate);

    // STRETCH slot `slot` at tempo `bpm10`: the cache to render, or nullopt (not STRETCH, empty or
    // already cached).
    std::optional<StretchKey> stretch_wanted(const model::Document& doc, int slot, int bpm10) const;
    void set_stretch(int slot, const StretchKey& key, model::SampleRef data);
    // Renders every missing cache for `bpm10` right away (offline renders and tests).
    void stretch_now(const model::Document& doc, int bpm10, std::uint32_t sample_rate);

private:
    struct FxEntry {
        model::FxType type = model::FxType::Off;
        float rate = 0.0f;
        std::shared_ptr<dsp::FxUnit> unit;
    };
    // Track inserts 0..31 (track * 2 + k), returns 32..33, master 34..35, VAPOR 36..38.
    static constexpr int kFxEntries = 39;
    dsp::FxUnit* fx_unit(int entry, model::FxType type, std::uint32_t rate, EngineSnapshot& s);
    void fill_instruments(const model::Document& doc, EngineSnapshot& s);
    void fill_loops(const model::Document& doc, EngineSnapshot& s);
    void fill_fx(const model::Document& doc, std::uint32_t rate, EngineSnapshot& s);

    std::array<FxEntry, kFxEntries> fx_{};
    std::array<std::pair<model::WavetableRef, std::shared_ptr<const WaveTables>>, model::kInstruments> tables_{};
    std::array<std::pair<StretchKey, model::SampleRef>, model::kLoopSlots> stretch_{};
};

} // namespace phos::engine
