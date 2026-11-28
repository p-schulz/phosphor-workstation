#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <future>
#include <map>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/cli.h"
#include "engine/engine.h"
#include "engine/midi_backend.h"
#include "engine/snapshot_builder.h"
#include "io/import.h"
#include "io/render_export.h"
#include "model/workspace.h"
#include "gfx/canvas.h"
#include "platform/crt_renderer_gl.h"
#include "platform/midi_out_thread.h"
#include "platform/window_glfw.h"
#include "ui/app_context.h"
#include "ui/display_profile.h"
#include "ui/settings.h"
#include "ui/ui.h"

namespace phos::app {

// The windowed workstation: owns the window, CRT renderer, UI and display profiles, and serves
// the UI as its AppContext.
class App final : public ui::AppContext {
public:
    explicit App(CliOptions options);
    ~App() override;

    int run();

    // AppContext
    const ui::DisplayStatus& display() const override { return display_; }
    const ui::DisplayProfile& profile() const override { return profile_; }
    void set_profile(const ui::DisplayProfile& profile) override;
    const ui::StatusInfo& status() const override { return status_; }
    double time_s() const override { return time_s_; }
    const ui::AudioInfo& audio() const override { return audio_info_; }
    const engine::AudioConfig& audio_config() const override { return settings_.audio; }
    void set_audio_config(const engine::AudioConfig& config) override;
    const ui::MidiInfo& midi() const override { return midi_info_; }
    const ui::MidiConfig& midi_config() const override { return settings_.midi; }
    void set_midi_config(const ui::MidiConfig& config) override;
    const ui::MidiOutConfig& midi_out_config() const override { return settings_.midi_out; }
    void set_midi_out_config(const ui::MidiOutConfig& config) override;
    int page_modifier() const override { return settings_.page_modifier; }
    bool follow() const override { return settings_.follow; }
    void set_follow(bool on) override {
        settings_.follow = on;
        save_settings();
    }
    void set_page_modifier(int mods) override {
        settings_.page_modifier = mods;
        save_settings();
    }
    void keyjazz_note(int note, bool on) override;
    void reset_latency() override;
    void learn_master_cc(bool on) override;
    model::Workspace& workspace() override { return workspace_; }
    const model::Workspace& workspace() const override { return workspace_; }
    void import_sample(const std::filesystem::path& path, int index) override;
    const std::string& import_status() const override { return import_status_; }
    void audition_file(const std::filesystem::path& path) override;
    void audition_instrument(int index, bool on) override;
    std::filesystem::path browse_folder() const override;
    void set_browse_folder(const std::filesystem::path& folder) override;
    const ui::ProjectInfo& project() const override { return project_; }
    std::filesystem::path projects_folder() const override;
    std::optional<std::string> run_host_command(const ui::Command& command) override;
    engine::LoopInfo loop_info() const override { return loop_info_; }

private:
    bool init();
    void load_settings();
    void choose_start_monitor();
    void refresh_display();
    void handle_monitor_changes();
    void run_exec_queue();
    std::filesystem::path snap_path(const std::string& requested, const char* prefix) const;
    std::string display_command(const ui::Command& command);

    // app_audio.cpp
    void init_audio();
    void open_audio();
    void update_audio();
    void shutdown_audio();
    std::optional<std::string> audio_command(const ui::Command& command);

    // app_midi.cpp
    void init_midi();
    void scan_midi();
    void scan_midi_outputs();
    void send_all_notes_off();
    void update_midi();
    void shutdown_midi();
    void publish_engine_snapshot();
    void save_settings();
    std::optional<std::string> midi_command(const ui::Command& command);

    // app_samples.cpp
    void update_samples();
    void add_audition(engine::EngineSnapshot& snapshot) const;

    // app_files.cpp
    void update_files();
    std::optional<std::string> files_command(const ui::Command& command);

    // app_loops.cpp
    void update_loops();
    std::optional<std::string> loop_command(const ui::Command& command);

    // app_song.cpp
    void init_song();
    void update_song();
    std::optional<std::string> song_command(const ui::Command& command);
    void add_recorded(const engine::RecEvent& r);
    void close_open_notes(std::uint32_t tick);
    bool confirm_discard(const std::string& verb);
    void open_document(model::Document doc, const std::filesystem::path& bundle, const std::string& name);
    std::filesystem::path autosave_bundle() const;

    CliOptions options_;
    std::filesystem::path config_dir_;
    std::filesystem::path profiles_path_;
    platform::Window window_;
    platform::CrtRenderer crt_;
    std::unique_ptr<ui::Ui> ui_;
    std::optional<ui::Fonts> fonts_;
    ui::KeyMap keys_;
    ui::DisplayProfiles profiles_;
    ui::DisplayProfile profile_;
    ui::DisplayStatus display_;
    ui::StatusInfo status_;
    gfx::Canvas canvas_;
    gfx::Canvas screen_; // canvas_ plus the side strips (side_w_ canvas pixels each side)
    int side_w_ = 0;
    double time_s_ = 0.0;
    std::deque<std::string> exec_;
    int wait_frames_ = 0;
    long frame_ = 0;
    std::string pending_shot_;
    std::vector<std::string> known_monitors_;

    std::filesystem::path settings_path_;
    ui::Settings settings_;
    ui::AudioInfo audio_info_;
    int tone_note_ = -1;
    // Declared before the backend so the backend (and its audio thread) goes first on destruction.
    std::unique_ptr<engine::Engine> engine_;
    std::unique_ptr<engine::AudioBackend> audio_;
    // Declared after the engine: MIDI callbacks push into the engine's queues, so MIDI goes first.
    std::unique_ptr<engine::MidiBackend> midi_backend_;
    ui::MidiInfo midi_info_;
    std::array<std::string, engine::kMaxMidiSlots> slot_names_; // open input per slot, "" = free
    std::array<std::string, engine::kMaxMidiOutSlots> out_names_; // open output per slot
    std::vector<std::pair<std::string, std::string>> out_errors_;
    platform::MidiOutThread midi_out_thread_; // stopped before the backend and the engine go
    std::atomic<double> out_delay_ns_{0.0};   // latency compensation + offset, read by that thread
    std::vector<std::pair<std::string, std::string>> midi_errors_; // input name -> last open error
    bool midi_scanned_ = false;
    double next_midi_scan_s_ = 0.0;
    double midi_activity_until_s_ = 0.0;
    std::uint64_t last_midi_events_ = 0;
    std::uint64_t snapshot_serial_ = 0;

    model::Workspace workspace_;
    std::uint64_t published_revision_ = 0;
    std::future<io::ImportResult> import_job_;
    int import_index_ = 0;
    std::string import_status_;
    std::future<io::ImportResult> audition_job_;
    io::ImportResult audition_;
    bool auditioning_ = false;
    engine::SnapshotBuilder builder_; // instruments, song, loops, effect units (cached between builds)
    std::uint32_t published_rate_ = 0;
    // STRETCH caches render one at a time on a worker; RESAMPLE renders offline on another.
    std::future<model::SampleRef> stretch_job_;
    int stretch_slot_ = -1;
    engine::StretchKey stretch_key_;
    std::future<model::SampleRef> resample_job_;
    int resample_target_ = 0; // 0..7 loop slot, 100.. instrument
    engine::LoopInfo loop_info_;
    bool import_to_loop_ = false;
    std::future<io::ExportResult> render_job_;

    ui::ProjectInfo project_;
    double next_autosave_s_ = 0.0;
    std::uint64_t autosaved_revision_ = 0;
    float sent_master_db_ = 1e9f;
    std::map<int, engine::RecEvent> open_notes_; // recording: track * 128 + note -> note-on
    std::string pending_confirm_;
    double pending_confirm_s_ = -100.0;
};

} // namespace phos::app
