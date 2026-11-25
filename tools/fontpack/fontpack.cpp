// fontpack: converts a PHOSPHOR glyph source (assets/fonts/src/*.txt) into the PNG atlas +
// metrics JSON loaded at runtime. Usage: fontpack <source.txt> <output-base>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "gfx/font.h"
#include "gfx/image_io.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: fontpack <source.txt> <output-base>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string error;
    const auto font = phos::gfx::parse_font_source(ss.str(), &error);
    if (!font) {
        std::fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
        return 1;
    }
    phos::gfx::Image atlas;
    atlas.channels = 1;
    atlas.pixels = phos::gfx::font_to_atlas(*font, &atlas.width, &atlas.height);
    const std::string base = argv[2];
    if (!phos::gfx::write_png(base + ".png", atlas)) {
        std::fprintf(stderr, "cannot write %s.png\n", base.c_str());
        return 1;
    }
    std::ofstream json(base + ".json", std::ios::binary | std::ios::trunc);
    json << phos::gfx::font_metrics_json(*font);
    if (!json) {
        std::fprintf(stderr, "cannot write %s.json\n", base.c_str());
        return 1;
    }
    int glyphs = 0;
    for (bool p : font->present) {
        glyphs += p ? 1 : 0;
    }
    std::printf("%s: %d glyphs, %dx%d cells -> %s.png/.json\n", font->name.c_str(), glyphs, font->cell_w,
                font->cell_h, base.c_str());
    return 0;
}
