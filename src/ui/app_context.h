#pragma once

#include <optional>
#include <string>

#include "gfx/canvas.h"
#include "ui/commands.h"
#include "ui/display_profile.h"

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

// Values shown by the screen chrome. Fixed defaults until the engine (M1) and sequencer (M4) exist.
struct StatusInfo {
    std::string project = "UNTITLED";
    double bpm = 120.0;
    int sig_num = 4;
    int sig_den = 4;
    int bar = 1;
    int beat = 1;
    int tick = 0;
    bool playing = false;
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

    // Host-level verbs (DISPLAY, SNAP, SHOT, WAIT, QUIT). Returns the INFO-line reply, or
    // nullopt when the host does not handle this verb.
    virtual std::optional<std::string> run_host_command(const Command& command) = 0;
};

} // namespace phos::ui
