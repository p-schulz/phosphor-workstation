#include <cctype>
#include <cstdio>
#include <filesystem>

#include "app/cli.h"
#include "gfx/image_io.h"
#include "gfx/palette.h"
#include "platform/paths.h"
#include "ui/render.h"

namespace phos::app {

int run_headless(const CliOptions& options) {
    if (options.render_page.empty()) {
        std::fprintf(stderr, "--headless: nothing to do (use --render-page)\n");
        return 2;
    }
    std::string error;
    const auto fonts = ui::load_fonts(platform::exe_dir() / "assets", &error);
    if (!fonts) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    ui::RenderRequest req;
    req.page = options.render_page;
    req.theme = options.theme;
    req.sample = options.sample;
    req.folder = options.folder;
    req.view = options.view;
    req.instrument = options.instrument - 1;
    if (!options.project.empty()) {
        req.project_path = options.project;
        req.project_name = std::filesystem::path(options.project).stem().string();
        for (auto& ch : req.project_name) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
    }
    const auto canvas = ui::render_page(req, *fonts, &error);
    if (!canvas) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    const gfx::Image img = gfx::canvas_to_rgb(*canvas, *gfx::find_theme(req.theme));
    if (!gfx::write_png(options.out, img)) {
        std::fprintf(stderr, "error: cannot write %s\n", options.out.c_str());
        return 1;
    }
    std::fprintf(stdout, "wrote %s (%dx%d, page %s, theme %s)\n", options.out.c_str(), img.width, img.height,
                 req.page.c_str(), req.theme.c_str());
    return 0;
}

} // namespace phos::app
