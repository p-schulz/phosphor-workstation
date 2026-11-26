#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <sstream>
#include <vector>

#include "dsp/analysis.h"
#include "engine/engine.h"
#include "engine/synth_voice.h"
#include "engine/wavetables.h"
#include "io/project.h"
#include "model/wavetable.h"
#include "seq/compiled_song.h"

using namespace phos;
using Catch::Approx;

namespace {

constexpr std::uint32_t kRate = 48000;

std::unique_ptr<engine::Engine> engine_with(const model::Instrument& inst, std::shared_ptr<const engine::WaveTables>* keep = nullptr) {
    auto e = std::make_unique<engine::Engine>();
    auto s = std::make_unique<engine::EngineSnapshot>();
    auto& b = s->instruments[0];
    b = engine::sampler_block(inst.sample.get(), inst.sampler);
    b.kind = inst.kind;
    b.wave = inst.wave;
    b.synth = inst.synth;
    b.drum = inst.drum;
    if (inst.wavetable) {
        s->table_refs[0] = engine::build_tables(*inst.wavetable);
        b.tables = s->table_refs[0].get();
        if (keep) {
            *keep = s->table_refs[0];
        }
    }
    e->publish(std::move(s));
    return e;
}

std::vector<float> render(engine::Engine& e, std::size_t frames) {
    std::vector<float> out(frames * 2);
    for (std::size_t pos = 0; pos < frames; pos += 256) {
        e.render_offline(out.data() + pos * 2, static_cast<std::uint32_t>(std::min<std::size_t>(256, frames - pos)), 2);
    }
    return out;
}

double crossings_hz(const std::vector<float>& v, std::size_t from, std::size_t to) {
    int n = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
        n += (v[(i - 1) * 2] < 0.0f && v[i * 2] >= 0.0f) ? 1 : 0;
    }
    return n * static_cast<double>(kRate) / static_cast<double>(to - from);
}

float peak(const std::vector<float>& v, std::size_t from_frame, std::size_t to_frame) {
    float p = 0.0f;
    for (std::size_t i = from_frame * 2; i < std::min(to_frame * 2, v.size()); ++i) {
        p = std::max(p, std::fabs(v[i]));
    }
    return p;
}

model::Instrument single_harmonic_table(std::vector<int> harmonic_per_segment, int cycles) {
    model::WavetableData d;
    for (const int h : harmonic_per_segment) {
        model::WaveSegment seg;
        seg.harmonics[static_cast<std::size_t>(h - 1)] = 1.0f;
        seg.cycles = cycles;
        d.segments.push_back(seg);
    }
    model::Instrument inst = model::make_instrument(model::InstrumentKind::Wavetable, "T");
    inst.wavetable = std::make_shared<const model::WavetableData>(model::wavetable::compute(d, false));
    inst.wave = model::WavetableParams{0, 0, false, false};
    inst.sampler.amp = model::Adsr{1.0f, 10.0f, 1.0f, 50.0f};
    return inst;
}

} // namespace

TEST_CASE("COMPUTE matches the analytic sum of sines", "[wavetable]") {
    model::WavetableData d;
    d.segments.resize(5);
    std::uint32_t rng = 12345;
    for (auto& seg : d.segments) {
        for (auto& a : seg.harmonics) {
            rng = rng * 1664525u + 1013904223u;
            a = static_cast<float>(rng >> 8) / static_cast<float>(1u << 24); // 0..1
        }
    }
    const auto out = model::wavetable::compute(d, false);
    for (std::size_t s = 0; s < d.segments.size(); ++s) {
        for (int i = 0; i < 128; ++i) {
            double expected = 0.0;
            for (int h = 1; h <= 32; ++h) {
                expected += d.segments[s].harmonics[static_cast<std::size_t>(h - 1)] * std::sin(2.0 * std::numbers::pi * h * i / 128.0);
            }
            CHECK(out.segments[s].wave[static_cast<std::size_t>(i)] == Approx(expected).margin(1e-5));
        }
    }
    // Normalised: the same shape with a peak of exactly 1.
    const auto norm = model::wavetable::compute(d, true);
    float p = 0.0f;
    for (const auto& seg : norm.segments) {
        for (const float v : seg.wave) {
            p = std::max(p, std::fabs(v));
        }
    }
    CHECK(p == Approx(1.0f));
    const float ratio = norm.segments[2].wave[10] / out.segments[2].wave[10];
    CHECK(norm.segments[4].wave[77] == Approx(out.segments[4].wave[77] * ratio).epsilon(1e-5));
}

TEST_CASE("wave tables are band-limited per mip level", "[wavetable]") {
    model::WavetableData d;
    d.segments.resize(1);
    for (int h = 1; h <= 32; ++h) {
        d.segments[0].harmonics[static_cast<std::size_t>(h - 1)] = 1.0f / h;
    }
    d = model::wavetable::compute(d, false);
    const auto t = engine::build_tables(d);
    for (int level = 0; level < engine::WaveTables::kLevels; ++level) {
        std::vector<std::complex<double>> x(128);
        for (int i = 0; i < 128; ++i) {
            x[static_cast<std::size_t>(i)] = t->table(0, level)[i];
        }
        dsp::fft(x);
        const int keep = 64 >> level;
        for (int k = 1; k < 64; ++k) {
            INFO("level " << level << " harmonic " << k);
            const double mag = std::abs(x[static_cast<std::size_t>(k)]) / 64.0;
            if (k > keep) {
                CHECK(mag < 1e-6);
            } else if (k <= 32) {
                CHECK(mag == Approx(1.0 / k).margin(1e-4));
            }
        }
    }
    CHECK(engine::WaveTables::level_for(110.0, 24000.0) == 0);    // 64 * 110 Hz < 24 kHz
    CHECK(engine::WaveTables::level_for(2637.0, 24000.0) == 3);   // only 8 harmonics fit
    CHECK(engine::WaveTables::level_for(15000.0, 24000.0) == 6);
}

TEST_CASE("wavetable voice plays the note pitch and steps through segments", "[wavetable][engine]") {
    // Segment 0: fundamental only; segment 1: second harmonic only. 20 cycles each, no loop.
    auto inst = single_harmonic_table({1, 2}, 20);
    auto e = engine_with(inst);
    e->send(engine::EngineCommand::midi_message(0x90, 69, 127, 0));
    const auto out = render(*e, kRate / 4);
    const std::size_t seg_frames = 20 * kRate / 440; // 2181
    CHECK(crossings_hz(out, 200, seg_frames - 50) == Approx(440.0).margin(25.0));
    CHECK(crossings_hz(out, seg_frames + 200, kRate / 4) == Approx(880.0).margin(15.0)); // stays on the last segment
}

TEST_CASE("synth oscillators play in tune, mono legato keeps one voice", "[synth][engine]") {
    model::Instrument inst = model::make_instrument(model::InstrumentKind::Synth, "S");
    inst.sampler.filter = model::FilterType::Off;
    inst.synth.osc1 = model::OscWave::Saw;
    inst.synth.osc2 = model::OscWave::Off;
    auto e = engine_with(inst);
    e->send(engine::EngineCommand::midi_message(0x90, 57, 127, 0)); // A3 = 220 Hz
    auto out = render(*e, kRate / 2);
    // A falling saw crosses zero upward once per cycle in the middle of the ramp.
    CHECK(crossings_hz(out, 4800, kRate / 2) == Approx(220.0).margin(3.0));

    inst.synth.mono = true;
    inst.synth.glide_ms = 30.0f;
    e = engine_with(inst);
    e->send(engine::EngineCommand::midi_message(0x90, 57, 127, 0));
    render(*e, 4800);
    e->send(engine::EngineCommand::midi_message(0x90, 69, 127, 0)); // legato to A4
    out = render(*e, kRate / 2);
    CHECK(e->read_meters().voices == 1);
    CHECK(crossings_hz(out, kRate / 4, kRate / 2) == Approx(440.0).margin(5.0)); // after the glide
}

TEST_CASE("PolyBLEP saw has far less aliasing than a naive saw", "[synth]") {
    // A 5 kHz saw at 48 kHz: aliases land between the harmonics.
    constexpr int n = 4096;
    const double dt = 5000.0 / kRate;
    std::vector<std::complex<double>> blep(n);
    std::vector<std::complex<double>> naive(n);
    double ph = 0.0;
    std::uint32_t noise = 1;
    for (int i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n);
        blep[static_cast<std::size_t>(i)] = engine::SynthVoice::osc(model::OscWave::Saw, ph, dt, 0.5f, noise) * hann;
        naive[static_cast<std::size_t>(i)] = (2.0 * ph - 1.0) * hann;
        ph += dt;
        ph -= std::floor(ph);
    }
    dsp::fft(blep);
    dsp::fft(naive);
    auto alias_energy = [&](const std::vector<std::complex<double>>& x) {
        double e = 0.0;
        for (int k = 1; k < n / 2; ++k) {
            const double hz = static_cast<double>(k) * kRate / n;
            const double h = hz / 5000.0;
            if (std::fabs(h - std::round(h)) > 0.1) { // away from every true harmonic
                e += std::norm(x[static_cast<std::size_t>(k)]);
            }
        }
        return e;
    };
    CHECK(alias_energy(blep) < alias_energy(naive) * 0.25);
}

TEST_CASE("a beat plays with zero samples loaded", "[drum][engine]") {
    model::Song song;
    song.metronome = model::MetronomeMode::Off;
    song.sections[0].bpm10 = 1200;
    song.tracks[0].instrument = 0;
    auto& lane = song.patterns[0].lanes[0];
    for (std::uint32_t q = 0; q < 4; ++q) {
        model::insert_note(lane, model::Note{q * 960, 120, static_cast<std::uint8_t>(q % 2 == 0 ? 36 : 38), 110}); // kick, snare
        model::insert_note(lane, model::Note{q * 960 + 480, 120, 42, 90});                                         // hats
    }
    model::Instrument kit = model::make_instrument(model::InstrumentKind::Drum, "KIT");
    auto e = engine_with(kit);
    auto snap = std::make_unique<engine::EngineSnapshot>();
    snap->instruments[0] = engine::sampler_block(nullptr, kit.sampler);
    snap->instruments[0].kind = model::InstrumentKind::Drum;
    snap->instruments[0].drum = kit.drum;
    snap->song = seq::compile(song);
    e->publish(std::move(snap));
    render(*e, 256);
    e->send(engine::EngineCommand::transport(engine::TransportAction::Play));
    const auto out = render(*e, kRate * 2); // one bar at 120 BPM
    for (int q = 0; q < 8; ++q) { // every eighth has a hit
        const std::size_t at = static_cast<std::size_t>(q) * kRate / 4 + 256;
        INFO("eighth " << q);
        CHECK(peak(out, at, at + 960) > 0.05f);
    }
    // Kick is low and snare/hat high: compare spectral centroids of the first 40 ms.
    auto centroid = [&](std::size_t at) {
        std::vector<float> mono;
        for (std::size_t i = at; i < at + 2048; ++i) {
            mono.push_back(out[i * 2]);
        }
        return dsp::analyze(mono, 1, kRate).centroid_hz;
    };
    CHECK(centroid(256) < centroid(kRate / 4 + 256)); // kick lower than the hat
    CHECK(e->read_track_peaks()[0] > 0.0f);
}

TEST_CASE("choke group: a closed hat cuts the open hat", "[drum][engine]") {
    model::Instrument kit = model::make_instrument(model::InstrumentKind::Drum, "KIT");
    auto e = engine_with(kit);
    e->send(engine::EngineCommand::midi_message(0x90, 46, 120, 0)); // open hat, 400 ms
    render(*e, 2400);
    CHECK(e->read_meters().voices == 1);
    e->send(engine::EngineCommand::midi_message(0x90, 42, 120, 0)); // closed hat, same group
    render(*e, 960);                                                // 20 ms: the open one has faded
    CHECK(e->read_meters().voices == 1);
    e->send(engine::EngineCommand::midi_message(0x90, 36, 120, 0)); // kick: no group, both ring
    render(*e, 256);
    CHECK(e->read_meters().voices == 2);
}

TEST_CASE("projects keep every instrument kind; version 1 files migrate", "[project]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "phosphor_kinds_test";
    fs::remove_all(dir);
    model::Document doc;
    doc.instruments[0] = model::make_instrument(model::InstrumentKind::Wavetable, "WAVE");
    doc.instruments[0].wave.authentic = true;
    doc.instruments[1] = model::make_instrument(model::InstrumentKind::Synth, "PAD");
    doc.instruments[1].synth.unison = 3;
    doc.instruments[1].synth.lfo_shape = model::LfoShape::SampleHold;
    doc.instruments[2] = model::make_instrument(model::InstrumentKind::Drum, "KIT");
    doc.instruments[2].drum[5].choke = 3;
    std::string error;
    REQUIRE(io::save_project(dir / "K.phosphor", doc, &error));
    const auto loaded = io::load_project(dir / "K.phosphor", 48000, &error);
    REQUIRE(loaded);
    CHECK(loaded->instruments[0].kind == model::InstrumentKind::Wavetable);
    CHECK(*loaded->instruments[0].wavetable == *doc.instruments[0].wavetable);
    CHECK(loaded->instruments[0].wave == doc.instruments[0].wave);
    CHECK(loaded->instruments[1].synth == doc.instruments[1].synth);
    CHECK(loaded->instruments[2].drum == doc.instruments[2].drum);
    CHECK(io::project_json(*loaded) == io::project_json(doc));
    fs::remove_all(dir);

    std::string v1 = R"({"schemaVersion": 1, "instruments": [{"name": "OLD"}], "song": {}})";
    REQUIRE(io::migrate_project(v1, &error));
    CHECK(v1.find("\"schemaVersion\": 5") != std::string::npos); // v1 -> ... -> v5
    CHECK(v1.find("\"kind\": \"SAMPLER\"") != std::string::npos);
}

TEST_CASE("incremental table builds equal full builds", "[wavetable]") {
    auto a = model::wavetable::resize(model::wavetable::default_table(), 40);
    const auto full_a = engine::build_tables(a);
    auto b = model::wavetable::set_points(a, 7, 10, 90, 0.9f, -0.4f);
    const auto incremental = engine::build_tables(b, &a, full_a.get());
    const auto full_b = engine::build_tables(b);
    CHECK(incremental->data == full_b->data);
    CHECK(incremental->cycles == full_b->cycles);
}
