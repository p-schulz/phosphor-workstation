#include "engine/snapshot_builder.h"

#include <algorithm>
#include <cmath>

#include "dsp/fx.h"
#include "dsp/stretch.h"
#include "engine/wavetables.h"

namespace phos::engine {

model::SampleRef render_stretch(const model::SampleData& source, const StretchKey& key, std::uint32_t sample_rate) {
    const double seconds = key.beats * 600.0 / std::max(1, key.bpm10);
    const auto out_frames = static_cast<std::size_t>(std::llround(seconds * sample_rate));
    const dsp::StretchInput in{source.left.data(), source.channels == 2 ? source.right.data() : nullptr,
                               source.frames, source.sample_rate};
    dsp::StretchOutput out = dsp::stretch(in, out_frames, key.shift, true);
    return model::make_sample(source.name + " STRETCH", source.source_path, sample_rate, std::move(out.left),
                              std::move(out.right));
}

SnapshotBuilder::SnapshotBuilder() = default;
SnapshotBuilder::~SnapshotBuilder() = default;

std::unique_ptr<EngineSnapshot> SnapshotBuilder::build(const model::Document& doc, std::uint32_t sample_rate) {
    auto s = std::make_unique<EngineSnapshot>();
    fill_instruments(doc, *s);
    s->song = seq::compile(doc.song);
    fill_loops(doc, *s);
    fill_fx(doc, sample_rate, *s);
    return s;
}

void SnapshotBuilder::fill_instruments(const model::Document& doc, EngineSnapshot& s) {
    for (int i = 0; i < model::kInstruments; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        const model::Instrument& inst = doc.instruments[ii];
        SamplerBlock& b = s.instruments[ii];
        b = sampler_block(inst.sample.get(), inst.sampler);
        b.kind = inst.kind;
        b.wave = inst.wave;
        b.synth = inst.synth;
        b.drum = inst.drum;
        b.gb = inst.gb;
        b.snes = inst.snes;
        b.brr = inst.brr.get();
        s.brr_refs[ii] = inst.brr;
        s.sample_refs[ii] = inst.sample;
        if (inst.kind == model::InstrumentKind::Wavetable && inst.wavetable) {
            // Band-limited tables are rebuilt only when the wavetable data changed (pointer).
            auto& cache = tables_[ii];
            if (cache.first != inst.wavetable) {
                cache = {inst.wavetable, build_tables(*inst.wavetable, cache.first.get(), cache.second.get())};
            }
            b.tables = cache.second.get();
            s.table_refs[ii] = cache.second;
        }
    }
    s.current_instrument = doc.current;
}

void SnapshotBuilder::fill_loops(const model::Document& doc, EngineSnapshot& s) {
    for (int i = 0; i < model::kLoopSlots; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        const model::LoopSlot& l = doc.song.loops[ii];
        LoopBlock& b = s.loops[ii];
        b.sample = l.sample.get();
        b.beats = l.beats();
        b.sync = l.sync;
        b.quantize = l.quantize;
        b.slices = l.slices;
        b.gain = std::pow(10.0f, l.gain_db / 20.0f);
        b.lp_hz = l.lp_hz;
        b.reverse = l.reverse;
        b.screw = l.screw;
        b.track = l.track;
        s.loop_refs[ii * 2] = l.sample;
        const auto& [key, data] = stretch_[ii];
        if (l.sync == model::LoopSync::Stretch && data && key.source == l.sample.get() && key.shift == l.shift &&
            key.beats == l.beats()) {
            b.stretched = data.get();
            b.stretch_bpm10 = key.bpm10;
            s.loop_refs[ii * 2 + 1] = data;
        }
    }
}

dsp::FxUnit* SnapshotBuilder::fx_unit(int entry, model::FxType type, std::uint32_t rate, EngineSnapshot& s) {
    FxEntry& e = fx_[static_cast<std::size_t>(entry)];
    if (type == model::FxType::Off) {
        e = FxEntry{}; // switched off: the next unit starts silent
        return nullptr;
    }
    if (!e.unit || e.type != type || e.rate != static_cast<float>(rate)) {
        e.type = type;
        e.rate = static_cast<float>(rate);
        e.unit = dsp::make_fx_unit(type, static_cast<float>(rate));
    }
    s.fx_refs.push_back(e.unit);
    return e.unit.get();
}

void SnapshotBuilder::fill_fx(const model::Document& doc, std::uint32_t rate, EngineSnapshot& s) {
    const model::Song& song = doc.song;
    for (int t = 0; t < model::kTracks; ++t) {
        for (int k = 0; k < 2; ++k) {
            s.fx.tracks[static_cast<std::size_t>(t)][static_cast<std::size_t>(k)] =
                fx_unit(t * 2 + k, song.tracks[static_cast<std::size_t>(t)].inserts[static_cast<std::size_t>(k)].type, rate, s);
        }
    }
    for (int k = 0; k < 2; ++k) {
        s.fx.returns[static_cast<std::size_t>(k)] = fx_unit(32 + k, song.returns[static_cast<std::size_t>(k)].type, rate, s);
        s.fx.master[static_cast<std::size_t>(k)] = fx_unit(34 + k, song.master_fx[static_cast<std::size_t>(k)].type, rate, s);
    }
    // VAPOR units exist once the macro was turned up, so moving it never reallocates.
    const bool vapor = song.vapor > 0 || fx_[36].unit;
    constexpr std::array<model::FxType, 3> kVapor = {model::FxType::Plate, model::FxType::Lofi, model::FxType::Tape};
    for (int k = 0; k < 3; ++k) {
        s.fx.vapor[static_cast<std::size_t>(k)] =
            fx_unit(36 + k, vapor ? kVapor[static_cast<std::size_t>(k)] : model::FxType::Off, rate, s);
    }
}

std::optional<StretchKey> SnapshotBuilder::stretch_wanted(const model::Document& doc, int slot, int bpm10) const {
    const model::LoopSlot& l = doc.song.loops[static_cast<std::size_t>(slot)];
    if (l.sync != model::LoopSync::Stretch || !l.sample || l.sample->frames == 0) {
        return std::nullopt;
    }
    const StretchKey key{l.sample.get(), bpm10, l.shift, l.beats()};
    const auto& cached = stretch_[static_cast<std::size_t>(slot)];
    if (cached.second && cached.first == key) {
        return std::nullopt;
    }
    return key;
}

void SnapshotBuilder::set_stretch(int slot, const StretchKey& key, model::SampleRef data) {
    stretch_[static_cast<std::size_t>(slot)] = {key, std::move(data)};
}

void SnapshotBuilder::stretch_now(const model::Document& doc, int bpm10, std::uint32_t sample_rate) {
    for (int i = 0; i < model::kLoopSlots; ++i) {
        if (const auto key = stretch_wanted(doc, i, bpm10)) {
            set_stretch(i, *key, render_stretch(*doc.song.loops[static_cast<std::size_t>(i)].sample, *key, sample_rate));
        }
    }
}

} // namespace phos::engine
