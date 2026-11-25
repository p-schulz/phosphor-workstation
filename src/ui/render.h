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
    std::optional<std::string> run_host_command(const Command& command) override;

    StatusInfo& mutable_status() { return status_; }

private:
    DisplayStatus display_;
    DisplayProfile profile_;
    StatusInfo status_;
};

// Loads assets/fonts/phosphor_8x12 and phosphor_8x8 from `asset_dir`.
std::optional<Fonts> load_fonts(const std::filesystem::path& asset_dir, std::string* error);

struct RenderRequest {
    std::string page = "1";
    std::string theme = "GREEN";
    std::string project_name = "UNTITLED";
};

// Renders one page exactly as the windowed app draws it (without light pen). The canvas holds
// palette indices; resolve with gfx::canvas_to_rgb(canvas, *gfx::find_theme(request.theme)).
std::optional<gfx::Canvas> render_page(const RenderRequest& request, const Fonts& fonts, std::string* error);

} // namespace phos::ui
