// Instrument library: NAME.phinst bundles (owner request after M8).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "content/gm.h"
#include "engine/offline.h"
#include "io/instrument_io.h"
#include "io/project.h"
#include "io/smf.h"
#include "io/snes.h"
#include "model/wavetable.h"
#include "model/workspace.h"
#include "ui/render.h"
#include "ui/ui.h"

using namespace phos;

namespace {

std::vector<float> tone(double hz, std::size_t frames) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        v[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / 48000.0));
    }
    return v;
}

std::filesystem::path scratch(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    return dir;
}

} // namespace

TEST_CASE("Every instrument kind survives export and import as a .phinst bundle", "[library]") {
    const auto dir = scratch("phosphor_library_test");
    std::vector<model::Instrument> kinds;
    model::Instrument smp;
    smp.name = "PLUCK";
    smp.sample = model::make_sample("PLUCK", "/somewhere/pluck.wav", 48000, tone(440.0, 9600), tone(442.0, 9600));
    smp.sampler.loop = model::LoopMode::Forward;
    smp.sampler.loop_start = 1200;
    smp.sampler.loop_end = 9000;
    smp.sampler.cutoff_hz = 3000.0f;
    kinds.push_back(smp);
    auto wave = model::make_instrument(model::InstrumentKind::Wavetable, "GLASS");
    wave.wave.authentic = true;
    kinds.push_back(wave);
    auto synth = model::make_instrument(model::InstrumentKind::Synth, "FAT BASS");
    synth.synth.unison = 4;
    synth.synth.osc1 = model::OscWave::Pulse;
    kinds.push_back(synth);
    auto drum = model::make_instrument(model::InstrumentKind::Drum, "808 KIT");
    drum.drum[3].tune = -5.0f;
    kinds.push_back(drum);
    auto gb = model::make_instrument(model::InstrumentKind::Gb, "GB LEAD");
    gb.gb.duty = 1;
    gb.gb.macros[model::kMacroVolume] = model::GbMacro{{15, 12, 9}, 1, 2};
    kinds.push_back(gb);
    auto snes = model::make_instrument(model::InstrumentKind::Snes, "SNES STR");
    snes.sample = model::make_sample("STR", "", 48000, tone(220.0, 24000));
    snes.sampler.loop = model::LoopMode::Forward;
    snes.sampler.loop_start = 4800;
    snes.sampler.loop_end = 24000;
    snes.snes.attack = 10;
    snes.brr = io::encode_for_snes(*snes.sample, snes.sampler, snes.snes);
    kinds.push_back(snes);

    for (const auto& inst : kinds) {
        CAPTURE(inst.name);
        const auto path = io::instrument_path(dir, inst.name);
        CHECK(path.filename().string() == inst.name + ".phinst");
        std::string error;
        REQUIRE(io::save_instrument(path, inst, &error));
        CHECK(io::is_instrument_bundle(path));
        const auto back = io::load_instrument(path, 48000, &error);
        INFO(error);
        REQUIRE(back);
        // Same parameters; audio equal by value (new objects).
        model::Instrument a = *back;
        model::Instrument b = inst;
        CHECK((a.sample == nullptr) == (b.sample == nullptr));
        if (a.sample) {
            CHECK(a.sample->left == b.sample->left);
            CHECK(a.sample->right == b.sample->right);
            CHECK(a.sample->source_path == b.sample->source_path);
        }
        CHECK((a.wavetable == nullptr) == (b.wavetable == nullptr));
        if (a.wavetable) {
            CHECK(*a.wavetable == *b.wavetable);
        }
        CHECK((a.brr == nullptr) == (b.brr == nullptr));
        if (a.brr) {
            CHECK(a.brr->data == b.brr->data);
            CHECK(a.brr->loop_block == b.brr->loop_block);
        }
        a.sample = b.sample = nullptr;
        a.wavetable = b.wavetable = nullptr;
        a.brr = b.brr = nullptr;
        CHECK(a == b);
    }
    CHECK(io::list_instruments(dir).size() == kinds.size());

    // Re-exporting under the same name replaces the old audio.
    model::Instrument smaller = smp;
    smaller.sample = model::make_sample("SHORT", "", 48000, tone(330.0, 480));
    std::string error;
    REQUIRE(io::save_instrument(io::instrument_path(dir, "PLUCK"), smaller, &error));
    std::size_t wavs = 0;
    for (const auto& e : std::filesystem::directory_iterator(io::instrument_path(dir, "PLUCK") / "samples")) {
        wavs += e.path().extension() == ".wav";
    }
    CHECK(wavs == 1);
    CHECK(io::load_instrument(io::instrument_path(dir, "PLUCK"), 48000, &error)->sample->frames == 480);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Instrument bundles from older versions migrate; bad bundles are refused", "[library]") {
    const auto dir = scratch("phosphor_library_migrate");
    std::filesystem::create_directories(dir / "OLD.phinst");
    {
        std::ofstream f(dir / "OLD.phinst" / io::kInstrumentFile);
        f << R"({"schemaVersion": 1, "instrument": {"name": "OLD", "rootNote": 48}})";
    }
    std::string error;
    const auto old = io::load_instrument(dir / "OLD.phinst", 48000, &error);
    INFO(error);
    REQUIRE(old);
    CHECK(old->name == "OLD");
    CHECK(old->kind == model::InstrumentKind::Sampler); // v1 -> v2 adds the kind
    CHECK(old->sampler.root_note == 48);
    std::filesystem::create_directories(dir / "BAD.phinst");
    {
        std::ofstream f(dir / "BAD.phinst" / io::kInstrumentFile);
        f << "{\"schemaVersion\": 5}";
    }
    CHECK_FALSE(io::load_instrument(dir / "BAD.phinst", 48000, &error));
    CHECK_FALSE(io::load_instrument(dir / "NONE.phinst", 48000, &error));
    CHECK(io::instrument_path(dir, "/abs/x.phinst") == std::filesystem::path("/abs/x.phinst"));
    std::filesystem::remove_all(dir);
}


TEST_CASE("EXPORT INST, IMPORT <name> and LIBRARY move instruments between projects", "[library][ui]") {
    const auto dir = scratch("phosphor_library_ui");
    std::string error;
    const auto fonts = ui::load_fonts(PHOS_ASSET_DIR, &error);
    REQUIRE(fonts);
    ui::HeadlessContext a;
    a.set_projects_folder(dir);
    ui::Ui ua(a, *fonts, ui::KeyMap::defaults(false));
    model::Document doc;
    doc.instruments[2] = model::make_instrument(model::InstrumentKind::Gb, "CHIP LEAD");
    doc.instruments[2].gb.duty = 0;
    doc.current = 2;
    a.workspace().replace(doc);
    REQUIRE(ua.execute("EXPORT INST"));
    CHECK(io::is_instrument_bundle(dir / "Instruments" / "CHIP LEAD.phinst"));
    REQUIRE(ua.execute("EXPORT INST MY LEAD"));
    CHECK(io::is_instrument_bundle(dir / "Instruments" / "MY LEAD.phinst"));

    // Another project: load it by name into instrument 5, undo puts the old one back.
    ui::HeadlessContext b;
    b.set_projects_folder(dir);
    ui::Ui ub(b, *fonts, ui::KeyMap::defaults(false));
    REQUIRE(ub.execute("INST 5"));
    REQUIRE(ub.execute("IMPORT MY LEAD"));
    const auto& inst = b.workspace().doc().instruments[4];
    CHECK(inst.name == "MY LEAD");
    CHECK(inst.kind == model::InstrumentKind::Gb);
    CHECK(inst.gb.duty == 0);
    REQUIRE(ub.execute("UNDO"));
    CHECK(b.workspace().doc().instruments[4].kind == model::InstrumentKind::Sampler);
    REQUIRE(ub.execute("LIBRARY"));
    CHECK(ub.current_page_id() == "2");
    CHECK(b.browse_folder() == dir / "Instruments");
    CHECK_FALSE(ub.execute("IMPORT NOT THERE.phinst"));
    std::filesystem::remove_all(dir);
}


TEST_CASE("IMPORT song.mid assembles a playable project: library first, GM presets, one undo step", "[library][midi]") {
    const auto dir = scratch("phosphor_midi_assembly");
    const auto lib = dir / "Instruments";
    std::string error;
    REQUIRE(io::save_instrument(io::instrument_path(lib, "BASS"), model::make_instrument(model::InstrumentKind::Gb, "MY GB BASS"), &error));
    auto kit = model::make_instrument(model::InstrumentKind::Drum, "MY KIT");
    kit.drum[0].tune = -3.0f;
    REQUIRE(io::save_instrument(io::instrument_path(lib, "DRUMS"), kit, &error));

    // A song to import: BASS (no program), strings (program 49 = ENSEMBLE), drums on channel 10,
    // a CC 7 fade on the strings.
    model::Document src;
    model::Song& s = src.song;
    s.tracks[0].name = "Bass";
    s.tracks[1].name = "Strings";
    s.tracks[1].program = 48;
    s.tracks[1].out_channel = 1;
    s.tracks[2].name = "Kit";
    s.tracks[2].out_channel = 9;
    model::insert_note(s.patterns[0].lanes[0], model::Note{0, 480, 36, 100});
    model::insert_note(s.patterns[0].lanes[1], model::Note{0, 3840, 60, 80});
    model::insert_note(s.patterns[0].lanes[2], model::Note{0, 240, 36, 120});
    auto& fade = s.patterns[0].lane_for(1, model::kCcVolume);
    fade.values[0] = 100;
    fade.values[8] = 40;
    REQUIRE(io::write_smf(dir / "song.mid", src, "SONG", &error));

    ui::HeadlessContext ctx;
    ctx.set_projects_folder(dir);
    const auto fonts = ui::load_fonts(PHOS_ASSET_DIR, &error);
    REQUIRE(fonts);
    ui::Ui ui(ctx, *fonts, ui::KeyMap::defaults(false));
    model::Document before;
    before.instruments[3] = model::make_instrument(model::InstrumentKind::Synth, "KEEP ME"); // not touched
    ctx.workspace().replace(before);
    REQUIRE(ui.execute("IMPORT " + (dir / "song.mid").string()));
    const model::Document& d = ctx.workspace().doc();
    CHECK(d.instruments[0].name == "MY GB BASS"); // library, by the track name
    CHECK(d.instruments[0].kind == model::InstrumentKind::Gb);
    CHECK(d.instruments[1].name == "ENSEMBLE");   // GM preset for program 49
    CHECK(d.instruments[1].kind == model::InstrumentKind::Synth);
    CHECK(d.instruments[2].name == "MY KIT");     // library DRUMS for channel 10
    CHECK(d.instruments[2].drum[0].tune == -3.0f);
    CHECK(d.instruments[3].name == "KEEP ME");
    CHECK(d.song.tracks[1].instrument == 1);
    CHECK(d.song.tracks[1].program == 48);
    CHECK(d.song.tracks[1].out_channel == 1);
    CHECK(d.song.tracks[2].out_channel == 9);
    const model::AutomationLane* lane = d.song.patterns[0].lane(1, model::kCcVolume);
    REQUIRE(lane);
    CHECK(lane->values[0] == 100);
    CHECK(lane->values[8] == 40);
    CHECK(ui.message_text().find("2 LIBRARY 1 PRESET") != std::string::npos);

    REQUIRE(ui.execute("UNDO")); // song and instruments come back together
    CHECK(ctx.workspace().doc().instruments[0].name == "EMPTY");
    CHECK(ctx.workspace().doc().song.patterns[0].lanes[0].empty());
    REQUIRE(ui.execute("REDO"));
    CHECK(ctx.workspace().doc().instruments[0].name == "MY GB BASS");

    CHECK(std::string(content::gm_family(0)) == "PIANO");
    CHECK(std::string(content::gm_family(33)) == "BASS");
    CHECK(std::string(content::gm_family(81)) == "SYNTH LEAD");
    CHECK(std::string(content::gm_family(-1)).empty());
    std::filesystem::remove_all(dir);
}


TEST_CASE("The example library and projects load and play", "[library][examples]") {
    const std::filesystem::path root = std::filesystem::path(PHOS_EXAMPLES_DIR);
    std::string error;
    const auto lib = io::list_instruments(root / "library");
    CHECK(lib.size() >= 40);
    for (const auto& p : lib) {
        INFO(p.filename().string());
        CHECK(io::load_instrument(p, 48000, &error));
    }
    for (const char* name : {"GB ADVENTURE", "SNES FIELD", "VAPOR PLAZA", "WAVEFORMS"}) {
        CAPTURE(name);
        auto doc = io::load_project(root / "projects" / (std::string(name) + ".phosphor"), 48000, &error);
        INFO(error);
        REQUIRE(doc);
        // The first section, 4 s: it makes sound and stays below full scale.
        doc->song.play_mode = model::PlayMode::Pattern;
        doc->song.current_section = doc->song.order.front();
        engine::SongRender opt;
        opt.tail_s = 0.0;
        const auto r = engine::render_song(*doc, opt);
        double e = 0.0;
        float peak = 0.0f;
        for (const float x : r.left) {
            REQUIRE(std::isfinite(x));
            e += static_cast<double>(x) * x;
            peak = std::max(peak, std::fabs(x));
        }
        CHECK(20.0 * std::log10(std::sqrt(e / static_cast<double>(r.left.size()))) > -35.0);
        CHECK(peak < 0.95f);
    }
    const auto midi = io::read_smf(root / "midi" / "GB ADVENTURE.mid", model::Song{}, &error);
    REQUIRE(midi);
    CHECK(midi->tracks == 7);
}
