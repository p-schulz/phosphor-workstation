#include <catch2/catch_test_macros.hpp>

#include <string>

#include "gfx/image_io.h"
#include "gfx/palette.h"
#include "golden/golden_list.h"
#include "ui/render.h"

using namespace phos;

TEST_CASE("page renders match the golden images", "[golden]") {
    std::string error;
    const auto fonts = ui::load_fonts(PHOS_ASSET_DIR, &error);
    REQUIRE(fonts);
    for (const auto& g : test::kGoldenPages) {
        INFO("golden " << g.file << " (update with the update_goldens target if the change is intended)");
        ui::RenderRequest req;
        req.page = std::string(g.page);
        req.theme = std::string(g.theme);
        if (!g.sample.empty()) {
            req.sample = std::string(PHOS_FIXTURE_DIR) + "/" + std::string(g.sample);
        }
        if (!g.project.empty()) {
            req.project_path = std::string(PHOS_FIXTURE_DIR) + "/" + std::string(g.project);
        }
        req.instrument = g.instrument;
        req.view = std::string(g.view);
        const auto canvas = ui::render_page(req, *fonts, &error);
        REQUIRE(canvas);
        const gfx::Image actual = gfx::canvas_to_rgb(*canvas, *gfx::find_theme(req.theme));
        const auto expected = gfx::read_png_rgb(std::string(PHOS_GOLDEN_DIR) + "/" + std::string(g.file));
        REQUIRE(expected);
        REQUIRE(expected->width == actual.width);
        REQUIRE(expected->height == actual.height);
        std::size_t diff = 0;
        for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
            diff += actual.pixels[i] != expected->pixels[i] ? 1 : 0;
        }
        CHECK(diff == 0);
    }
}
