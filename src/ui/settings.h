#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/audio_backend.h"
#include "engine/messages.h"
#include "ui/input.h"

namespace phos::ui {

// "OFF", "CTRL", "ALT", "CMD" <-> 0, kModCtrl, kModAlt, kModSuper.
const char* page_modifier_name(int mods);
std::optional<int> page_modifier_from(std::string_view name);

struct MidiInputSetting {
    std::string name;
    bool enabled = true; // new inputs are opened automatically
    int channel = -1;    // -1 = all channels (OMNI), 0..15 = this channel only
    friend bool operator==(const MidiInputSetting&, const MidiInputSetting&) = default;
};

struct MidiConfig {
    std::vector<MidiInputSetting> inputs; // every input seen so far, by name
    engine::Placement placement = engine::Placement::Immediate;
    engine::VelocityCurve velocity_curve = engine::VelocityCurve::Linear;
    int bend_range = 2;       // semitones, 0..24
    int keyjazz_octave = 4;   // lower row starts at C<octave> (C4 = MIDI 60)
    int keyjazz_velocity = 100;
    int master_cc = -1;       // CC bound to MASTER by CC learn, -1 = none
    int master_cc_channel = -1; // -1 = any channel

    MidiInputSetting* find(std::string_view name);
    const MidiInputSetting* find(std::string_view name) const;
    friend bool operator==(const MidiConfig&, const MidiConfig&) = default;
};

// MIDI out (PLAN.md §7.7, M8): off by default. Outputs are remembered by name like the inputs.
struct MidiOutputSetting {
    std::string name;
    bool enabled = false; // new outputs stay closed until switched on
    bool clock = false;   // MIDI clock, start / continue / stop, song position
    friend bool operator==(const MidiOutputSetting&, const MidiOutputSetting&) = default;
};

struct MidiOutConfig {
    bool enabled = false;
    std::vector<MidiOutputSetting> outputs;
    int offset_ms = 0;          // -100..100, added to every message (align external synths)
    bool latency_comp = true;   // also delay by the audio output latency (buffer x periods)
    bool thru = true;           // live input -> the current track's output
    MidiOutputSetting* find(std::string_view name);
    const MidiOutputSetting* find(std::string_view name) const;
    friend bool operator==(const MidiOutConfig&, const MidiOutConfig&) = default;
};

// settings.json in the settings folder (PLAN.md §7.9): audio device (M1) and MIDI input (M2).
struct Settings {
    static constexpr int kSchemaVersion = 1;

    engine::AudioConfig audio;
    MidiConfig midi;
    MidiOutConfig midi_out;
    std::string browse_folder; // Page 2: last folder
    std::string projects_folder; // empty = ~/Documents/PHOSPHOR
    // Modifier that, held with a page id key (0-8, C, D, K, L, M, R, S, H; P = back), jumps to that
    // page: kModCtrl / kModAlt / kModSuper, 0 = off. Default CTRL on macOS (MOD is Cmd there),
    // ALT elsewhere (MOD is Ctrl).
    int page_modifier = kDefaultPageModifier;
    // Page R follows the playhead while playing (scrolls the grid, shows the playing section).
    bool follow = true;
#if defined(__APPLE__)
    static constexpr int kDefaultPageModifier = kModCtrl;
#else
    static constexpr int kDefaultPageModifier = kModAlt;
#endif

    std::string to_json() const;
    // Missing keys keep their defaults; out-of-range values are clamped.
    static std::optional<Settings> from_json(std::string_view text, std::string* error);
    static std::optional<Settings> load(const std::filesystem::path& path, std::string* error);
    bool save(const std::filesystem::path& path, std::string* error) const;
};

} // namespace phos::ui
