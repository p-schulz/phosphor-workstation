#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "gfx/canvas.h"
#include "gfx/font.h"

using namespace phos::gfx;

namespace {
std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("glyph source parsing", "[font]") {
    const char* src = "name t\ncell 8 4\ntop 1\nlines 3\nglyph 0x41\n#.#\n.#.\n#######.\nglyph 0x42\n";
    std::string error;
    const auto f = parse_font_source(src, &error);
    REQUIRE(f);
    CHECK(f->cell_w == 8);
    CHECK(f->cell_h == 4);
    CHECK(f->has('A'));
    CHECK(f->has('B'));
    CHECK_FALSE(f->has('C'));
    const auto rows = f->glyph_rows('A');
    CHECK(rows[0] == 0);
    CHECK(rows[1] == 0xA0);
    CHECK(rows[2] == 0x40);
    CHECK(rows[3] == 0xFE);
    CHECK_FALSE(parse_font_source("cell 8 4\nglyph 0x41\n#########\n", &error)); // wider than cell
    CHECK_FALSE(parse_font_source("cell 8 4\nglyph 0x41\nglyph 0x41\n", &error));  // duplicate
}

TEST_CASE("atlas round trip", "[font]") {
    std::string error;
    const auto f = parse_font_source("name t\ncell 8 12\nglyph 0x41\n#\n.#\n..#\n", &error);
    REQUIRE(f);
    int w = 0;
    int h = 0;
    const auto atlas = font_to_atlas(*f, &w, &h);
    const auto back = font_from_atlas(atlas, w, h, font_metrics_json(*f), &error);
    REQUIRE(back);
    CHECK(*back == *f);
}

TEST_CASE("shipped font atlases match their glyph sources (run the `fonts` target)", "[font]") {
    for (const char* name : {"phosphor_8x12", "phosphor_8x8"}) {
        INFO(name);
        std::string error;
        const auto src = parse_font_source(read(std::string(PHOS_ASSET_DIR) + "/fonts/src/" + name + ".txt"), &error);
        INFO(error);
        REQUIRE(src);
        const auto packed = load_font(std::string(PHOS_ASSET_DIR) + "/fonts/" + name, &error);
        INFO(error);
        REQUIRE(packed);
        CHECK(*packed == *src);
        for (int c = 0x20; c < 0x7F; ++c) {
            INFO("code " << c);
            CHECK(packed->has(static_cast<unsigned char>(c)));
        }
    }
}

TEST_CASE("text draws glyph bits and advances", "[font]") {
    std::string error;
    const auto f = parse_font_source("cell 8 2\nglyph 0x41\n##\n#.\n", &error);
    REQUIRE(f);
    Canvas c(32, 4);
    const int end = draw_text(c, *f, 1, 1, "AA", 3);
    CHECK(end == 17);
    CHECK(c.get(1, 1) == 3);
    CHECK(c.get(2, 1) == 3);
    CHECK(c.get(2, 2) == 0);
    CHECK(c.get(9, 1) == 3);
}
