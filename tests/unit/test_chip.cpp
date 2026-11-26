#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <set>
#include <vector>

#include "engine/engine.h"
#include "engine/snes_bus.h"
#include "engine/snes_voice.h"
#include "engine/wavetables.h"
#include "io/project.h"
#include "model/chip.h"
#include "model/wavetable.h"

using namespace phos;
using Catch::Approx;

namespace {

constexpr std::uint32_t kRate = 48000;

std::unique_ptr<engine::Engine> engine_with(const model::Instrument& inst, bool strict = false) {
    auto e = std::make_unique<engine::Engine>();
    auto s = std::make_unique<engine::EngineSnapshot>();
    for (int slot = 0; slot < 4; ++slot) { // the same instrument in slots 0..3
        auto& b = s->instruments[static_cast<std::size_t>(slot)];
        b = engine::sampler_block(inst.sample.get(), inst.sampler);
        b.kind = inst.kind;
        b.gb = inst.gb;
        b.snes = inst.snes;
        b.brr = inst.brr.get();
        b.wave = inst.wave;
        if (inst.wavetable) {
            s->table_refs[static_cast<std::size_t>(slot)] = engine::build_tables(*inst.wavetable);
            b.tables = s->table_refs[static_cast<std::size_t>(slot)].get();
        }
        s->brr_refs[static_cast<std::size_t>(slot)] = inst.brr;
    }
    s->song.strict_cmi = strict;
    s->song.strict_gb = strict;
    s->song.strict_snes = strict;
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

// Frequency from rising zero crossings with linear interpolation of the crossing times.
double measure_hz(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double first = -1.0;
    double last = -1.0;
    int n = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
        const float a = v[(i - 1) * 2];
        const float b = v[i * 2];
        if (a < 0.0f && b >= 0.0f) {
            const double t = static_cast<double>(i - 1) + a / (a - b);
            if (first < 0.0) {
                first = t;
            } else {
                ++n;
            }
            last = t;
        }
    }
    return n > 0 ? n * kRate / (last - first) : 0.0;
}

model::Instrument gb(model::GbChannel ch) {
    model::Instrument inst = model::make_instrument(model::InstrumentKind::Gb, "GB");
    inst.gb = model::default_gb(ch);
    inst.gb.macros = {};      // steady tone: no macros
    for (int i = 0; i < 32; ++i) { // one square cycle per 32 samples: clean zero crossings
        inst.gb.waves[0][static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i < 16 ? 13 : 3);
    }
    inst.gb.vibrato_cents = 0;
    inst.gb.env_pace = 0;
    return inst;
}

std::vector<std::int16_t> sine_pcm(double hz, double rate, std::size_t n, double amp) {
    std::vector<std::int16_t> pcm(n);
    for (std::size_t i = 0; i < n; ++i) {
        pcm[i] = static_cast<std::int16_t>(std::lround(amp * 32767.0 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate)));
    }
    return pcm;
}

} // namespace

TEST_CASE("GB period formulas", "[chip][gb]") {
    CHECK(model::gb_pulse_hz(0) == Approx(64.0));
    CHECK(model::gb_pulse_hz(1750) == Approx(131072.0 / 298.0));
    CHECK(model::gb_wave_hz(1024) == Approx(64.0));
    CHECK(model::gb_noise_hz(0, 0) == Approx(524288.0)); // divider 0 counts as 0.5
    CHECK(model::gb_noise_hz(3, 4) == Approx(262144.0 / 48.0));
    CHECK(model::gb_pulse_period(440.0) == 1750);       // 2048 - 131072/440 = 1750.1
    CHECK(model::gb_wave_period(440.0) == 1899);        // 2048 - 65536/440 = 1899.05
}

TEST_CASE("GB pulse and wave voices sound at the period-quantised frequency", "[chip][gb]") {
    for (const auto ch : {model::GbChannel::Pulse, model::GbChannel::Wave}) {
        for (const int note : {45, 57, 69, 81, 93}) {
            auto e = engine_with(gb(ch));
            e->send(engine::EngineCommand::midi_message(0x90, static_cast<std::uint8_t>(note), 127, 0));
            const auto out = render(*e, kRate / 2);
            const double ideal = 440.0 * std::exp2((note - 69) / 12.0);
            const double expected = ch == model::GbChannel::Pulse ? model::gb_pulse_hz(model::gb_pulse_period(ideal))
                                                                  : model::gb_wave_hz(model::gb_wave_period(ideal));
            INFO((ch == model::GbChannel::Pulse ? "pulse" : "wave") << " note " << note << ": expected " << expected);
            CHECK(measure_hz(out, 4800, kRate / 2) == Approx(expected).epsilon(0.002));
        }
    }
    // The quantisation is audible high up: A7 (3520 Hz) lands on period 2011 = 3542.5 Hz, 0.64 % sharp.
    CHECK(model::gb_pulse_period(3520.0) == 2011);
    CHECK(model::gb_pulse_hz(2011) == Approx(3542.486).epsilon(1e-5));
}

TEST_CASE("GB LFSR periods are 32767 and 127", "[chip][gb]") {
    for (const bool short_mode : {false, true}) {
        std::uint16_t s = 0; // reset value (Pan Docs)
        for (int i = 0; i < 70000; ++i) { // settle into the cycle
            s = model::gb_lfsr_step(s, short_mode);
        }
        const std::uint16_t start = short_mode ? static_cast<std::uint16_t>(s & 0x7F) : s;
        int period = 0;
        do {
            s = model::gb_lfsr_step(s, short_mode);
            ++period;
        } while ((short_mode ? static_cast<std::uint16_t>(s & 0x7F) : s) != start && period < 100000);
        CHECK(period == (short_mode ? 127 : 32767));
    }
}

TEST_CASE("BRR decoder follows the documented filter equations", "[chip][snes]") {
    // Hand-made blocks; expected values worked out from fullsnes "BRR Samples":
    //   sample = (nibble SHL shift) SAR 1, then the filter, clamp to 16 bits, clip to 15 bits.
    int old = 0;
    int older = 0;
    std::int16_t out[16];
    // Filter 0, shift 12: nibbles 7, -8, 1, -1, then zeros.
    const std::uint8_t f0[9] = {0xC0, 0x78, 0x1F, 0, 0, 0, 0, 0, 0};
    model::brr_decode_block(f0, out, old, older);
    CHECK(out[0] == 14336);  // (7 << 12) >> 1
    CHECK(out[1] == -16384); // (-8 << 12) >> 1
    CHECK(out[2] == 2048);
    CHECK(out[3] == -2048);
    CHECK(out[4] == 0);
    // Filter 1 (new = s + old + (-old SAR 4)), shift 4, nibbles 2, 2, 0, 0: from old = 0.
    old = older = 0;
    const std::uint8_t f1[9] = {0x44, 0x22, 0x00, 0, 0, 0, 0, 0, 0};
    model::brr_decode_block(f1, out, old, older);
    CHECK(out[0] == 16);                     // 0 + (2 << 4 >> 1)
    CHECK(out[1] == 16 + 16 + ((-16) >> 4)); // 31
    CHECK(out[2] == 31 + ((-31) >> 4));      // 31 - 2 = 29
    CHECK(out[3] == 29 + ((-29) >> 4));      // 27
    // Filter 2 with a known history: old = 1000, older = 500, sample 0.
    old = 1000;
    older = 500;
    const std::uint8_t f2[9] = {0x08, 0, 0, 0, 0, 0, 0, 0, 0};
    model::brr_decode_block(f2, out, old, older);
    CHECK(out[0] == 1000 * 2 + ((-1000 * 3) >> 5) - 500 + (500 >> 4)); // 1906 - 500 + 31 = 1437
    // Filter 3: old = 1000, older = 500.
    old = 1000;
    older = 500;
    const std::uint8_t f3[9] = {0x0C, 0, 0, 0, 0, 0, 0, 0, 0};
    model::brr_decode_block(f3, out, old, older);
    CHECK(out[0] == 1000 * 2 + ((-1000 * 13) >> 6) - 500 + ((500 * 3) >> 4)); // 2000 - 204 - 500 + 93 = 1389
    // 15-bit clip: 0x3000 + 0x2000 = 0x5000 wraps to -0x3000 ("+4000h..+7FFFh -> -4000h..-1").
    old = 0x3000;
    older = 0;
    const std::uint8_t clipblk[9] = {0xC4, 0x40, 0, 0, 0, 0, 0, 0, 0}; // filter 1, nibble 4 at shift 12 = 0x2000
    model::brr_decode_block(clipblk, out, old, older);
    CHECK(out[0] == static_cast<std::int16_t>(0x3000 + ((-0x3000) >> 4) + 0x2000 - 0x8000));
}

TEST_CASE("BRR encode -> decode of a 440 Hz sine keeps >= 20 dB SNR (and the baseline)", "[chip][snes]") {
    const auto pcm = sine_pcm(440.0, 32000.0, 32000, 0.5);
    const auto brr = model::brr_encode(pcm, -1, 32000);
    CHECK(brr.blocks() == 2000);
    const auto dec = model::brr_decode(brr);
    double sig = 0.0;
    double err = 0.0;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const double t = pcm[i] >> 1; // the DSP works in 15 bits
        sig += t * t;
        err += (dec[i] - t) * (dec[i] - t);
    }
    const double snr = 10.0 * std::log10(sig / err);
    WARN("BRR 440 Hz sine SNR: " << snr << " dB");
    constexpr double kBaselineDb = 54.5; // measured 2026-11-26 (docs/checklists/m6.md); fail on > 1 dB regression
    CHECK(snr >= 20.0);
    CHECK(snr >= kBaselineDb - 1.0);
}

TEST_CASE("echo FIR impulse response equals the coefficients", "[chip][snes]") {
    engine::SnesBus bus;
    bus.prepare(48000.0);
    model::SnesEcho e;
    e.delay = 1;   // 512 samples
    e.feedback = 0;
    e.vol_l = 127;
    e.vol_r = 127;
    e.fir = {10, -20, 30, -40, 50, 60, -5, 7};
    std::vector<float> out;
    for (int t = 0; t < 600; ++t) {
        float l = 0.0f;
        float r = 0.0f;
        bus.echo_frame(t == 0 ? 0.5f : 0.0f, 0.0f, e, l, r);
        out.push_back(l);
    }
    // The impulse leaves the 512-sample ring and passes the 8 FIR taps, newest (FIR7) first.
    for (int k = 0; k < 8; ++k) {
        const int t = 512 + (7 - k);
        INFO("FIR" << k);
        CHECK(out[static_cast<std::size_t>(t)] == Approx(0.5 * e.fir[static_cast<std::size_t>(k)] / 128.0 * 127.0 / 128.0).margin(1e-6));
    }
    for (int t = 0; t < 512; ++t) {
        CHECK(out[static_cast<std::size_t>(t)] == 0.0f);
    }
}

TEST_CASE("Gaussian table weights sum to 0x7FF..0x801", "[chip][snes]") {
    const auto& g = model::kSnesGauss;
    for (int i = 0; i < 256; ++i) {
        const int sum = g[static_cast<std::size_t>(i)] + g[static_cast<std::size_t>(0xFF - i)] +
                        g[static_cast<std::size_t>(0x100 + i)] + g[static_cast<std::size_t>(0x1FF - i)];
        CHECK(sum >= 0x7FF);
        CHECK(sum <= 0x801);
    }
    CHECK(engine::SnesVoice::pitch_register(32000, 60, 60, 0.0f, 1.0) == 0x1000);
    CHECK(engine::SnesVoice::pitch_register(16000, 72, 60, 0.0f, 1.0) == 0x1000);
}

TEST_CASE("SNES voice plays a looped BRR sine in tune through the 32 kHz bus", "[chip][snes][engine]") {
    // 64 samples per cycle at 32 kHz = 500 Hz; 4 cycles = 16 blocks loop seamlessly.
    std::vector<std::int16_t> pcm;
    for (int i = 0; i < 256; ++i) {
        pcm.push_back(static_cast<std::int16_t>(std::lround(16000.0 * std::sin(2.0 * std::numbers::pi * i / 64.0))));
    }
    model::Instrument inst = model::make_instrument(model::InstrumentKind::Snes, "SNES");
    inst.brr = std::make_shared<const model::BrrSample>(model::brr_encode(pcm, 0, 32000));
    inst.snes.root_note = 71;            // B4 ~ 493.9 Hz: the table is 500 Hz, so note 71 plays 500 Hz
    inst.snes.echo = false;
    auto e = engine_with(inst);
    e->send(engine::EngineCommand::midi_message(0x90, 71, 127, 0));
    const auto out = render(*e, kRate / 2);
    CHECK(measure_hz(out, 4800, kRate / 2) == Approx(500.0).epsilon(0.003));
    float peak = 0.0f;
    for (const float v : out) {
        peak = std::max(peak, std::fabs(v));
    }
    CHECK(peak > 0.1f);
}

TEST_CASE("STRICT modes limit voices: GB 4 channels, SNES 8, CMI 16", "[chip][strict]") {
    // GB: six pulse notes keep two (pulse 1 and 2); wave and noise add one each.
    {
        auto e = engine_with(gb(model::GbChannel::Pulse), true);
        for (int n = 0; n < 6; ++n) {
            e->send(engine::EngineCommand::sampler_note_on(0, 60 + n, 1.0f));
        }
        render(*e, 512);
        CHECK(e->read_meters().voices == 2);
        auto snap = std::make_unique<engine::EngineSnapshot>();
        snap->song.strict_gb = true;
        for (int slot = 0; slot < 3; ++slot) {
            const auto ch = slot == 0 ? model::GbChannel::Pulse : slot == 1 ? model::GbChannel::Wave : model::GbChannel::Noise;
            auto& b = snap->instruments[static_cast<std::size_t>(slot)];
            b.kind = model::InstrumentKind::Gb;
            b.gb = gb(ch).gb;
            b.params.amp = model::Adsr{0.5f, 10.0f, 1.0f, 5.0f};
        }
        e->publish(std::move(snap));
        render(*e, 256);
        for (int n = 0; n < 3; ++n) {
            e->send(engine::EngineCommand::sampler_note_on(1, 50 + n, 1.0f)); // wave channel: mono
            e->send(engine::EngineCommand::sampler_note_on(2, 40 + n, 1.0f)); // noise channel: mono
        }
        render(*e, 512);
        CHECK(e->read_meters().voices == 4);
    }
    // SNES: twelve notes keep eight.
    {
        model::Instrument inst = model::make_instrument(model::InstrumentKind::Snes, "S");
        inst.brr = std::make_shared<const model::BrrSample>(model::brr_encode(sine_pcm(500.0, 32000.0, 1024, 0.4), 0, 32000));
        auto e = engine_with(inst, true);
        for (int n = 0; n < 12; ++n) {
            e->send(engine::EngineCommand::sampler_note_on(0, 50 + n, 1.0f));
        }
        render(*e, 1024);
        CHECK(e->read_meters().voices == 8);
        auto free = engine_with(inst, false);
        for (int n = 0; n < 12; ++n) {
            free->send(engine::EngineCommand::sampler_note_on(0, 50 + n, 1.0f));
        }
        render(*free, 1024);
        CHECK(free->read_meters().voices == 12);
    }
    // CMI: twenty wavetable notes keep sixteen.
    {
        auto e = engine_with(model::make_instrument(model::InstrumentKind::Wavetable, "W"), true);
        for (int n = 0; n < 20; ++n) {
            e->send(engine::EngineCommand::sampler_note_on(0, 40 + n, 1.0f));
        }
        render(*e, 512);
        CHECK(e->read_meters().voices == 16);
    }
}

TEST_CASE("projects keep GB and SNES instruments, echo and STRICT flags", "[chip][project]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "phosphor_chip_test";
    fs::remove_all(dir);
    model::Document doc;
    doc.instruments[0] = model::make_instrument(model::InstrumentKind::Gb, "LEAD");
    doc.instruments[0].gb.macros[model::kMacroArp] = model::GbMacro{{0, 4, 7}, 0, -1};
    doc.instruments[0].gb.waves[3][5] = 9;
    doc.instruments[1] = model::make_instrument(model::InstrumentKind::Snes, "PAD");
    doc.instruments[1].brr = std::make_shared<const model::BrrSample>(model::brr_encode(sine_pcm(300.0, 32000.0, 800, 0.3), 160, 24000));
    doc.instruments[1].snes.adsr = false;
    doc.instruments[1].snes.gain_mode = model::SnesGainMode::BentIncrease;
    doc.song.snes_echo.fir = model::kFirPresets[1];
    doc.song.snes_echo.delay = 7;
    doc.song.strict_gb = true;
    std::string error;
    REQUIRE(io::save_project(dir / "C.phosphor", doc, &error));
    const auto loaded = io::load_project(dir / "C.phosphor", 48000, &error);
    REQUIRE(loaded);
    CHECK(loaded->instruments[0].gb == doc.instruments[0].gb);
    CHECK(loaded->instruments[1].snes == doc.instruments[1].snes);
    CHECK(*loaded->instruments[1].brr == *doc.instruments[1].brr);
    CHECK(loaded->song.snes_echo == doc.song.snes_echo);
    CHECK(loaded->song.strict_gb);
    CHECK(io::project_json(*loaded) == io::project_json(doc));
    fs::remove_all(dir);
}

#include "core/rt_guard.h"

TEST_CASE("GB, SNES and the SNES bus run allocation-free", "[chip][realtime]") {
    if (!rt::guard_enabled()) {
        SKIP("allocation guard is compiled in Debug builds only");
    }
    rt::set_abort_on_violation(false);
    auto e = std::make_unique<engine::Engine>();
    auto s = std::make_unique<engine::EngineSnapshot>();
    const auto brr = std::make_shared<const model::BrrSample>(model::brr_encode(sine_pcm(400.0, 32000.0, 2048, 0.4), 512, 32000));
    for (int slot = 0; slot < 4; ++slot) {
        auto& b = s->instruments[static_cast<std::size_t>(slot)];
        b.kind = slot < 3 ? model::InstrumentKind::Gb : model::InstrumentKind::Snes;
        b.gb = model::default_gb(slot == 0 ? model::GbChannel::Pulse : slot == 1 ? model::GbChannel::Wave : model::GbChannel::Noise);
        b.brr = brr.get();
        b.snes.pitch_mod = true;
        b.snes.noise = slot == 3;
    }
    s->brr_refs[3] = brr;
    s->song.snes_echo.delay = 15;
    s->song.snes_echo.feedback = 90;
    e->publish(std::move(s));
    std::vector<float> out(1024 * 2);
    const auto before = rt::violations();
    for (int block = 0; block < 400; ++block) {
        if (block % 9 == 0) {
            e->send(engine::EngineCommand::sampler_note_on(block % 4, 40 + block % 40, 0.8f));
        }
        if (block % 13 == 0) {
            e->send(engine::EngineCommand::sampler_release(block % 4));
        }
        e->process(out.data(), block % 2 ? 1024u : 333u, 2, 0);
    }
    CHECK(rt::violations() == before);
    rt::set_abort_on_violation(true);
}

TEST_CASE("GB notes end after key-off, also with looping macros", "[chip][gb]") {
    // The default lead: a looping volume macro without a release point (the hanging-note bug).
    model::Instrument lead = model::make_instrument(model::InstrumentKind::Gb, "LEAD");
    REQUIRE(lead.gb.macros[model::kMacroVolume].loop >= 0);
    auto e = engine_with(lead);
    for (int n = 0; n < 4; ++n) {
        e->send(engine::EngineCommand::midi_message(0x90, static_cast<std::uint8_t>(60 + n * 4), 100, 0));
    }
    render(*e, kRate / 2);
    CHECK(e->read_meters().voices == 4);
    for (int n = 0; n < 4; ++n) {
        e->send(engine::EngineCommand::midi_message(0x80, static_cast<std::uint8_t>(60 + n * 4), 0, 0));
    }
    render(*e, kRate / 10);
    CHECK(e->read_meters().voices == 0);

    // With a release point: the release part plays out, then the note ends even though the macro
    // loops before the release point.
    lead.gb.macros[model::kMacroVolume] = model::GbMacro{{15, 12, 10, 10, 8, 4, 2, 1}, 1, 5};
    e = engine_with(lead);
    e->send(engine::EngineCommand::midi_message(0x90, 64, 100, 0));
    render(*e, kRate / 2);
    e->send(engine::EngineCommand::midi_message(0x80, 64, 0, 0));
    render(*e, kRate / 30); // ~1.5 ticks: still in the release part
    CHECK(e->read_meters().voices == 1);
    render(*e, kRate / 5);
    CHECK(e->read_meters().voices == 0);

    // Envelope only (no macros): key-off ends the note.
    model::Instrument plain = gb(model::GbChannel::Pulse);
    e = engine_with(plain);
    e->send(engine::EngineCommand::midi_message(0x90, 64, 100, 0));
    render(*e, 4800);
    e->send(engine::EngineCommand::midi_message(0x80, 64, 0, 0));
    render(*e, 4800);
    CHECK(e->read_meters().voices == 0);
}

TEST_CASE("PANIC silences every voice and forgets held notes and the pedal", "[engine]") {
    auto e = engine_with(model::make_instrument(model::InstrumentKind::Gb, "LEAD"));
    e->send(engine::EngineCommand::midi_message(0xB0, 64, 127, 0)); // sustain down
    for (int n = 0; n < 6; ++n) {
        e->send(engine::EngineCommand::midi_message(0x90, static_cast<std::uint8_t>(50 + n), 100, 0));
    }
    e->send(engine::EngineCommand::midi_message(0xE0, 0, 127, 0)); // bend up
    render(*e, 4800);
    REQUIRE(e->read_meters().voices == 6);
    e->send(engine::EngineCommand::panic());
    render(*e, 256);
    CHECK(e->read_meters().voices == 0);
    CHECK(e->read_midi_meters().held == std::array<std::uint64_t, 2>{0, 0});
    // The pedal is up again: a new note ends on key-off.
    e->send(engine::EngineCommand::midi_message(0x90, 60, 100, 0));
    render(*e, 2400);
    e->send(engine::EngineCommand::midi_message(0x80, 60, 0, 0));
    render(*e, 4800);
    CHECK(e->read_meters().voices == 0);
}
