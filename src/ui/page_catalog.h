#pragma once

#include <array>
#include <string_view>

namespace phos::ui {

// The page set (PLAN.md §8, provisional until docs/screens.md is approved).
struct PageInfo {
    std::string_view id;
    std::string_view name;      // INDEX list caption
    std::string_view short_name; // soft-key caption, ≤ 5 chars
    std::string_view milestone; // milestone that implements it; empty = available now
};

inline constexpr std::array<PageInfo, 15> kPageCatalog = {{
    {"1", "INDEX", "INDEX", ""},
    {"2", "DISK CONTROL", "DISK", "M3"},
    {"3", "KEYBOARD CONTROL", "KEYBD", "M2"},
    {"4", "HARMONIC PROFILES", "HARM", "M5"},
    {"5", "SYNTH", "SYNTH", "M5"},
    {"6", "WAVEFORM DRAWING", "WAVE", "M5"},
    {"7", "CONTROL PARAMETERS", "CTRL", "M3"},
    {"8", "SOUND SAMPLING", "SAMPL", "M3"},
    {"C", "CHIP VOICES", "CHIP", "M6"},
    {"D", "WAVEFORM DISPLAY", "DISPL", "M5"},
    {"L", "LOOP LIBRARY", "LOOPS", "M7"},
    {"M", "MIXER", "MIXER", "M4"},
    {"R", "REAL-TIME COMPOSER", "COMPO", "M4"},
    {"S", "SETUP", "SETUP", ""},
    {"H", "HELP", "HELP", ""},
}};

inline const PageInfo* find_page_info(std::string_view id) {
    for (const auto& p : kPageCatalog) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace phos::ui
