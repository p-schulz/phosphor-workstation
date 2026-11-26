#pragma once

#include <optional>
#include <string>

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "engine/audio_backend.h"
#include "engine/engine.h"
#include "gfx/canvas.h"
#include "model/workspace.h"
#include "ui/commands.h"
#include "ui/display_profile.h"
#include "ui/settings.h"

namespace phos::ui {

struct DisplayStatus {
    bool headless = true;
    std::string monitor_name = "HEADLESS";
    int monitor_index = 0; // 0-based
    int monitor_count = 1;
    int mode_w = 512; // monitor video mode (signal)
    int mode_h = 384;
    int refresh_hz = 60;
    int fb_w = 512; // window framebuffer
    int fb_h = 384;
    bool fullscreen = false;
    double pixel_aspect = 1.0;
    gfx::Rect viewport{0, 0, 512, 384};
};

// Audio device and engine state for Page S and the status line. Headless: available = false.
struct AudioInfo {
    bool available = false;
    engine::AudioStatus status;
    engine::EngineMeters meters;
    std::vector<std::string> devices; // output device names, system order
    bool guard_enabled = false;      // rt allocation guard compiled in (Debug)
    std::uint64_t guard_violations = 0;
    float master_db = 0.0f;
    int tone_note = -1; // sustained TONE note, -1 = none
    std::array<float, 17> bus_peaks{}; // tracks 1..16 + direct bus, decaying (Page M)
};

struct ProjectInfo {
    std::string name = "UNTITLED";
    std::filesystem::path bundle; // empty = never saved
    bool modified = false;
};

struct MidiPortState {
    std::string name;
    bool present = false; // connected right now
    bool open = false;    // receiving
    bool enabled = true;
    int channel = -1;     // -1 = all
    std::string error;
};

struct MidiOutPortState {
    std::string name;
    bool present = false;
    bool open = false;
    bool enabled = false;
    bool clock = false;
    std::string error;
};

// MIDI input state for Pages 3, K and S. Headless: available = false.
struct MidiInfo {
    std::vector<MidiOutPortState> outputs; // M8: known outputs, present ones first
    std::uint64_t out_sent = 0;            // messages the MIDI-out thread sent
    double out_worst_late_ms = 0.0;        // how late the latest-sent message left
    std::uint64_t out_lost = 0;            // engine queue full
    bool available = false;
    std::string api = "NONE";
    std::vector<MidiPortState> ports;   // known inputs (settings), present ones first
    engine::MidiMeters meters;
    engine::LatencyStats::View latency;
    bool activity = false;              // an event arrived in the last 150 ms
    bool learning = false;              // waiting for a CC to bind to MASTER
    std::string last_cc;                // "CC 7 = 100 CH 1", for the monitor line
};

// Values shown by the screen chrome. Transport fields stay at defaults until the sequencer (M4).
struct StatusInfo {
    std::string project = "UNTITLED";
    double bpm = 120.0;
    int sig_num = 4;
    int sig_den = 4;
    int bar = 1;
    int beat = 1;
    int tick = 0;
    bool playing = false;
    bool recording = false;
    bool count_in = false;
    int section = 0;        // playing (or current) section
    int song_step = 0;
    int pattern = 0;        // playing pattern
    std::uint32_t pattern_tick = 0; // playhead within the pattern
    float cpu = 0.0f; // 0..1
    int voices = 0;
    bool midi_activity = false;
    int instrument = 1;
    std::string instrument_name = "--------";
};

// What pages may ask of the host application. Implemented by the windowed app and by the
// headless renderer (HeadlessContext), so every page renders identically in both.
class AppContext {
public:
    virtual ~AppContext() = default;

    virtual const DisplayStatus& display() const = 0;
    // Profile in effect on the current monitor.
    virtual const DisplayProfile& profile() const = 0;
    // Applies and persists a changed profile for the current monitor.
    virtual void set_profile(const DisplayProfile& profile) = 0;
    virtual const StatusInfo& status() const = 0;
    // Seconds since start; drives cursor blink. Headless: fixed.
    virtual double time_s() const = 0;

    virtual const AudioInfo& audio() const = 0;
    virtual const engine::AudioConfig& audio_config() const = 0;
    // Reopens the device with `config` and persists it in settings.json.
    virtual void set_audio_config(const engine::AudioConfig& config) = 0;

    virtual const MidiInfo& midi() const = 0;
    virtual const MidiConfig& midi_config() const = 0;
    // Applies (opens/closes inputs, updates the engine) and persists in settings.json.
    virtual void set_midi_config(const MidiConfig& config) = 0;
    virtual const MidiOutConfig& midi_out_config() const = 0;
    virtual void set_midi_out_config(const MidiOutConfig& config) = 0;
    // Page key (settings.json "keys"): modifier + page id jumps to that page. 0 = off.
    virtual int page_modifier() const = 0;
    // Page R FOLLOW (settings.json "composer").
    virtual bool follow() const = 0;
    virtual void set_follow(bool on) = 0;
    virtual void set_page_modifier(int mods) = 0;
    // KEYJAZZ: computer-keyboard note on/off, handled by the engine like MIDI input.
    virtual void keyjazz_note(int note, bool on) = 0;
    virtual void reset_latency() = 0;
    // CC learn for MASTER: the next received CC is bound (on = start, off = cancel).
    virtual void learn_master_cc(bool on) = 0;

    // Instruments and undo (M3). Pages edit through Workspace::set_instrument; the host
    // republishes the engine snapshot when the revision moves.
    virtual model::Workspace& workspace() = 0;
    virtual const model::Workspace& workspace() const = 0;
    // Imports `path` into instrument `index` on a worker; the result is one undoable step.
    virtual void import_sample(const std::filesystem::path& path, int index) = 0;
    // "" when idle, else what the worker is doing ("IMPORTING X.WAV").
    virtual const std::string& import_status() const = 0;
    // Plays a file once at its root key without importing it (Page 2).
    virtual void audition_file(const std::filesystem::path& path) = 0;
    // Holds (on) / releases (off) instrument `index` at its root key.
    virtual void audition_instrument(int index, bool on) = 0;
    // Folder Page 2 opens in, and where it last was.
    virtual std::filesystem::path browse_folder() const = 0;
    virtual void set_browse_folder(const std::filesystem::path& folder) = 0;

    // Projects (M4).
    virtual const ProjectInfo& project() const = 0;
    virtual std::filesystem::path projects_folder() const = 0;

    // Host-level verbs (DISPLAY, SNAP, SHOT, WAIT, QUIT, AUDIO, TONE, CLICK, MASTER). Returns the INFO-line reply, or
    // nullopt when the host does not handle this verb.
    virtual std::optional<std::string> run_host_command(const Command& command) = 0;

    // Page L (M7): what each loop slot is doing. Headless: all stopped.
    virtual engine::LoopInfo loop_info() const { return {}; }
};

} // namespace phos::ui
