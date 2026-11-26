#pragma once

#include <array>
#include <string_view>

namespace phos::test {

struct GoldenPage {
    std::string_view page;
    std::string_view theme;
    std::string_view file;
    std::string_view sample = {};  // fixture loaded into instrument 1 (tests/fixtures/)
    std::string_view project = {}; // fixture project bundle (tests/fixtures/)
    int instrument = -1;           // instrument selected after loading
    std::string_view view = {};    // page view ("OUT" for Page 3)
};

// Every page implemented so far, plus the index in two more themes to cover palette resolution.
inline constexpr std::array<GoldenPage, 26> kGoldenPages = {{
    {"1", "GREEN", "page-1.png"},
    {"S", "GREEN", "page-s.png"},
    {"H", "GREEN", "page-h.png"},
    {"TESTCARD", "GREEN", "testcard.png"},
    {"1", "AMBER", "page-1-amber.png"},
    {"1", "VAPOR", "page-1-vapor.png"},
    {"3", "GREEN", "page-3.png"},
    {"K", "GREEN", "page-k.png"},
    {"7", "GREEN", "page-7.png", "loop_forward.wav"},
    {"8", "GREEN", "page-8.png", "loop_pingpong.wav"},
    {"7", "GREEN", "page-7-empty.png"},
    {"R", "GREEN", "page-r.png", {}, "demo.phosphor"},
    {"M", "GREEN", "page-m.png", {}, "demo.phosphor"},
    {"4", "GREEN", "page-4.png", {}, "synth.phosphor", 0},
    {"6", "GREEN", "page-6.png", {}, "synth.phosphor", 0},
    {"D", "GREEN", "page-d.png", {}, "synth.phosphor", 0},
    {"0", "GREEN", "page-0.png", {}, "synth.phosphor", 0},
    {"5", "GREEN", "page-5-synth.png", {}, "synth.phosphor", 1},
    {"5", "GREEN", "page-5-drum.png", {}, "synth.phosphor", 2},
    {"C", "GREEN", "page-c-gb.png", {}, "chip.phosphor", 0},
    {"C", "GREEN", "page-c-snes.png", {}, "chip.phosphor", 3},
    {"L", "GREEN", "page-l.png", {}, "vapor.phosphor"},
    {"M", "GREEN", "page-m-fx.png", {}, "vapor.phosphor"},
    {"L", "COLOR", "page-l-color.png", {}, "vapor.phosphor"}, // role colours
    {"D", "COLOR", "page-d-color.png", {}, "synth.phosphor", 0},
    {"3", "GREEN", "page-3-out.png", {}, "demo.phosphor", -1, "OUT"},
}};

} // namespace phos::test
