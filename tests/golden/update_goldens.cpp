// Rewrites tests/golden/images/*.png from the current renderer. Run only when a visual change is
// intended, then review the PNG diff before committing.
#include <cstdio>
#include <filesystem>
#include <string>

#include "gfx/image_io.h"
#include "gfx/palette.h"
#include "golden/golden_list.h"
#include "ui/render.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: golden_update <output-dir>\n");
        return 2;
    }
    const std::filesystem::path out_dir = argv[1];
    std::string error;
    const auto fonts = phos::ui::load_fonts(PHOS_ASSET_DIR, &error);
    if (!fonts) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::filesystem::create_directories(out_dir);
    for (const auto& g : phos::test::kGoldenPages) {
        phos::ui::RenderRequest req;
        req.page = std::string(g.page);
        req.theme = std::string(g.theme);
        const auto canvas = phos::ui::render_page(req, *fonts, &error);
        if (!canvas) {
            std::fprintf(stderr, "%s: %s\n", std::string(g.file).c_str(), error.c_str());
            return 1;
        }
        const auto path = out_dir / std::string(g.file);
        if (!phos::gfx::write_png(path, phos::gfx::canvas_to_rgb(*canvas, *phos::gfx::find_theme(req.theme)))) {
            std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.string().c_str());
    }
    return 0;
}
