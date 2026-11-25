#pragma once

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/cli.h"
#include "gfx/canvas.h"
#include "platform/crt_renderer_gl.h"
#include "platform/window_glfw.h"
#include "ui/app_context.h"
#include "ui/display_profile.h"
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
    std::optional<std::string> run_host_command(const ui::Command& command) override;

private:
    bool init();
    void load_settings();
    void choose_start_monitor();
    void refresh_display();
    void handle_monitor_changes();
    void run_exec_queue();
    std::filesystem::path snap_path(const std::string& requested, const char* prefix) const;
    std::string display_command(const ui::Command& command);

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
    gfx::Canvas screen_;
    double time_s_ = 0.0;
    std::deque<std::string> exec_;
    int wait_frames_ = 0;
    long frame_ = 0;
    std::string pending_shot_;
    std::vector<std::string> known_monitors_;
};

} // namespace phos::app
