#include <catch2/catch_test_macros.hpp>

#include "ui/commands.h"
#include "ui/settings.h"

using namespace phos;
using namespace phos::ui;

TEST_CASE("settings round trip through JSON", "[settings]") {
    Settings s;
    s.audio.device = "External Headphones";
    s.audio.sample_rate = 44100;
    s.audio.buffer_frames = 64;
    s.audio.exclusive = true;
    s.audio.enabled = false;
    std::string error;
    const auto back = Settings::from_json(s.to_json(), &error);
    REQUIRE(back);
    CHECK(back->audio == s.audio);
}

TEST_CASE("settings keep defaults for missing keys and clamp bad values", "[settings]") {
    const auto s = Settings::from_json(R"({"schemaVersion":1,"audio":{"bufferFrames":4,"device":"X"}})", nullptr);
    REQUIRE(s);
    CHECK(s->audio.buffer_frames == 32);
    CHECK(s->audio.device == "X");
    CHECK(s->audio.sample_rate == 48000);
    CHECK(s->audio.enabled);

    std::string error;
    CHECK_FALSE(Settings::from_json(R"({"schemaVersion":9})", &error));
    CHECK(error.find("schemaVersion") != std::string::npos);
    CHECK_FALSE(Settings::from_json("not json", &error));
}

TEST_CASE("note names parse with C4 = 60", "[commands]") {
    CHECK(parse_note("A4") == 69);
    CHECK(parse_note("a4") == 69);
    CHECK(parse_note("C4") == 60);
    CHECK(parse_note("C#3") == 49);
    CHECK(parse_note("Bb2") == 46);
    CHECK(parse_note("C-1") == 0);
    CHECK(parse_note("G9") == 127);
    CHECK(parse_note("60") == 60);
    CHECK_FALSE(parse_note("G#9"));
    CHECK_FALSE(parse_note("128"));
    CHECK_FALSE(parse_note("H2"));
    CHECK_FALSE(parse_note("A"));
}

TEST_CASE("midi settings round trip through JSON", "[settings]") {
    Settings s;
    s.midi.inputs = {{"Keystation 49", true, -1}, {"IAC Bus 1", false, 9}};
    s.midi.placement = engine::Placement::Stable;
    s.midi.velocity_curve = engine::VelocityCurve::Hard;
    s.midi.bend_range = 12;
    s.midi.keyjazz_octave = 3;
    s.midi.master_cc = 7;
    s.midi.master_cc_channel = 0;
    const auto back = Settings::from_json(s.to_json(), nullptr);
    REQUIRE(back);
    CHECK(back->midi == s.midi);
    // Older settings files without a midi section keep the defaults.
    const auto old = Settings::from_json(R"({"schemaVersion":1,"audio":{}})", nullptr);
    REQUIRE(old);
    CHECK(old->midi == MidiConfig{});
}
