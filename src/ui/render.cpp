#include "ui/render.h"

#include "core/text.h"
#include "gfx/palette.h"
#include "ui/layout.h"

namespace phos::ui {

HeadlessContext::HeadlessContext() { display_.viewport = gfx::Rect{0, 0, kCanvasW, kCanvasH}; }

std::optional<std::string> HeadlessContext::run_host_command(const Command& command) {
    if (command.verb == "DISPLAY" || command.verb == "SNAP" || command.verb == "SHOT" || command.verb == "QUIT" ||
        command.verb == "WAIT") {
        return "ERROR: " + command.verb + " NEEDS A WINDOW";
    }
    return std::nullopt;
}

std::optional<Fonts> load_fonts(const std::filesystem::path& asset_dir, std::string* error) {
    auto large = gfx::load_font(asset_dir / "fonts" / "phosphor_8x12", error);
    if (!large) {
        return std::nullopt;
    }
    auto small = gfx::load_font(asset_dir / "fonts" / "phosphor_8x8", error);
    if (!small) {
        return std::nullopt;
    }
    return Fonts{std::move(*large), std::move(*small)};
}

std::optional<gfx::Canvas> render_page(const RenderRequest& request, const Fonts& fonts, std::string* error) {
    if (!gfx::find_theme(request.theme)) {
        if (error) {
            *error = "unknown theme '" + request.theme + "'";
        }
        return std::nullopt;
    }
    HeadlessContext ctx;
    DisplayProfile profile = ctx.profile();
    profile.theme = std::string(gfx::find_theme(request.theme)->name);
    ctx.set_profile(profile);
    ctx.mutable_status().project = request.project_name;

    Ui ui(ctx, fonts, KeyMap::defaults(false));
    const std::string id = to_upper(request.page);
    if (id != "1" && !ui.go_to_page(id)) {
        if (error) {
            *error = ui.message_text();
        }
        return std::nullopt;
    }
    gfx::Canvas canvas(kCanvasW, kCanvasH);
    ui.draw(canvas);
    return canvas;
}

} // namespace phos::ui
