#include "ui/render.h"

#include <algorithm>

#include "core/text.h"
#include "io/import.h"
#include "io/project.h"
#include "gfx/palette.h"
#include "ui/layout.h"

namespace phos::ui {

HeadlessContext::HeadlessContext() { display_.viewport = gfx::Rect{0, 0, kCanvasW, kCanvasH}; }

std::optional<std::string> HeadlessContext::run_host_command(const Command& command) {
    if (command.verb == "DISPLAY" || command.verb == "SNAP" || command.verb == "SHOT" || command.verb == "QUIT" ||
        command.verb == "WAIT") {
        return "ERROR: " + command.verb + " NEEDS A WINDOW";
    }
    if (command.verb == "AUDIO" || command.verb == "TONE" || command.verb == "CLICK" || command.verb == "MASTER" ||
        command.verb == "MIDI" || command.verb == "PLAY" || command.verb == "STOP" || command.verb == "CONTINUE" ||
        command.verb == "REC" || command.verb == "NEW" || command.verb == "LOAD" || command.verb == "SAVE" ||
        command.verb == "SAVEAS" || command.verb == "RECOVER" || command.verb == "PANIC" || command.verb == "RENDER" ||
        command.verb == "RESAMPLE") {
        return "ERROR: " + command.verb + " NEEDS THE AUDIO ENGINE";
    }
    return std::nullopt;
}

void HeadlessContext::keyjazz_note(int note, bool on) {
    // Mirrors the engine's held-note bitmap so Page K renders the same way headless.
    auto it = std::find(keyjazz_held_.begin(), keyjazz_held_.end(), note);
    if (on && it == keyjazz_held_.end()) {
        keyjazz_held_.push_back(note);
    } else if (!on && it != keyjazz_held_.end()) {
        keyjazz_held_.erase(it);
    }
    const std::uint64_t bit = std::uint64_t{1} << (note & 63);
    auto& word = midi_.meters.held[static_cast<std::size_t>(note >> 6)];
    word = on ? (word | bit) : (word & ~bit);
}

void HeadlessContext::import_sample(const std::filesystem::path& path, int index) {
    const auto r = io::import_sample(path, {});
    if (!r.ok()) {
        import_status_ = "ERROR: " + to_upper(r.error);
        return;
    }
    model::Instrument inst;
    inst.name = to_upper(r.sample->name);
    inst.sample = r.sample;
    inst.sampler = r.params;
    workspace_.set_instrument(index, inst, "IMPORT");
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
    if (!request.project_path.empty()) {
        auto doc = io::load_project(request.project_path, 48000, error);
        if (!doc) {
            return std::nullopt;
        }
        ctx.workspace().replace(std::move(*doc));
    }
    if (request.instrument >= 0) {
        ctx.workspace().select(request.instrument);
    }
    if (!request.sample.empty()) {
        ctx.import_sample(request.sample, 0);
        if (!ctx.import_status().empty()) {
            if (error) {
                *error = ctx.import_status();
            }
            return std::nullopt;
        }
    }
    if (!request.folder.empty()) {
        ctx.set_browse_folder(request.folder);
    }

    Ui ui(ctx, fonts, KeyMap::defaults(false));
    const std::string id = to_upper(request.page);
    if (id != "1" && !ui.go_to_page(id)) {
        if (error) {
            *error = ui.message_text();
        }
        return std::nullopt;
    }
    if (!request.view.empty() && !ui.show_view(request.view)) {
        if (error) {
            *error = "page " + id + " has no view " + request.view;
        }
        return std::nullopt;
    }
    gfx::Canvas canvas(kCanvasW, kCanvasH);
    ui.draw(canvas);
    return canvas;
}

} // namespace phos::ui
