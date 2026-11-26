#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "gfx/canvas.h"
#include "ui/app_context.h"
#include "ui/ui.h"

namespace phos::ui {

// AppContext for rendering without a window or devices: fixed status values, a fixed clock and an
// in-memory display profile. Used by `--headless --render-page`, golden tests and update_goldens.
class HeadlessContext final : public AppContext {
public:
    HeadlessContext();

    const DisplayStatus& display() const override { return display_; }
    const DisplayProfile& profile() const override { return profile_; }
    void set_profile(const DisplayProfile& profile) override { profile_ = profile; }
    const StatusInfo& status() const override { return status_; }
    double time_s() const override { return 0.0; }
    const AudioInfo& audio() const override { return audio_; }
    const engine::AudioConfig& audio_config() const override { return audio_config_; }
    void set_audio_config(const engine::AudioConfig& config) override { audio_config_ = config; }
    const MidiInfo& midi() const override { return midi_; }
    const MidiConfig& midi_config() const override { return midi_config_; }
    void set_midi_config(const MidiConfig& config) override { midi_config_ = config; }
    const MidiOutConfig& midi_out_config() const override { return midi_out_; }
    void set_midi_out_config(const MidiOutConfig& config) override { midi_out_ = config; }
    int page_modifier() const override { return page_modifier_; }
    bool follow() const override { return follow_; }
    void set_follow(bool on) override { follow_ = on; }
    void set_page_modifier(int mods) override { page_modifier_ = mods; }
    void keyjazz_note(int note, bool on) override;
    void reset_latency() override {}
    void learn_master_cc(bool on) override { midi_.learning = on; }
    model::Workspace& workspace() override { return workspace_; }
    const model::Workspace& workspace() const override { return workspace_; }
    // Headless import is synchronous (render requests and tests).
    void import_sample(const std::filesystem::path& path, int index) override;
    const std::string& import_status() const override { return import_status_; }
    void audition_file(const std::filesystem::path&) override {}
    void audition_instrument(int, bool) override {}
    std::filesystem::path browse_folder() const override { return browse_folder_; }
    void set_browse_folder(const std::filesystem::path& folder) override { browse_folder_ = folder; }
    const ProjectInfo& project() const override { return project_; }
    std::filesystem::path projects_folder() const override { return projects_folder_; }
    void set_projects_folder(const std::filesystem::path& folder) { projects_folder_ = folder; }

    // Notes KEYJAZZ sent and has not released yet (tests).
    const std::vector<int>& keyjazz_held() const { return keyjazz_held_; }
    std::optional<std::string> run_host_command(const Command& command) override;

    StatusInfo& mutable_status() { return status_; }

private:
    DisplayStatus display_;
    DisplayProfile profile_;
    StatusInfo status_;
    AudioInfo audio_;
    engine::AudioConfig audio_config_;
    MidiInfo midi_;
    MidiConfig midi_config_;
    MidiOutConfig midi_out_;
    bool follow_ = true;
    int page_modifier_ = kModAlt; // fixed, so renders do not depend on the build platform
    std::vector<int> keyjazz_held_;
    model::Workspace workspace_;
    std::string import_status_;
    std::filesystem::path browse_folder_;
    std::filesystem::path projects_folder_; // tests: where EXPORT and LIBRARY write
    ProjectInfo project_;
};

// Loads assets/fonts/phosphor_8x12 and phosphor_8x8 from `asset_dir`.
std::optional<Fonts> load_fonts(const std::filesystem::path& asset_dir, std::string* error);

struct RenderRequest {
    std::string page = "1";
    std::string theme = "GREEN";
    std::string project_name = "UNTITLED";
    std::string sample;  // optional: imported into instrument 1 before rendering (--sample)
    std::string project_path; // optional: a .phosphor bundle loaded before rendering (--project)
    int instrument = -1;      // optional: select instrument 0..15 after loading (--inst N, 1-based on the CLI)
    std::string folder;  // optional: Page 2 folder (--folder)
    std::string view;    // optional: the page's view, as in "P3 OUT" (--view)
};

// Renders one page exactly as the windowed app draws it (without light pen). The canvas holds
// palette indices; resolve with gfx::canvas_to_rgb(canvas, *gfx::find_theme(request.theme)).
std::optional<gfx::Canvas> render_page(const RenderRequest& request, const Fonts& fonts, std::string* error);

} // namespace phos::ui
