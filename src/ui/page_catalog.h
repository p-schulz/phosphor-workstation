#pragma once

#include <array>
#include <string_view>

namespace phos::ui {

// The page set (PLAN.md §8, docs/screens.md §3).
struct PageInfo {
    std::string_view id;
    std::string_view name;      // INDEX list caption
    std::string_view short_name; // soft-key caption, ≤ 5 chars
    std::string_view milestone; // milestone that implements it; empty = available now
};

inline constexpr std::array<PageInfo, 17> kPageCatalog = {{
    {"1", "INDEX", "INDEX", ""},
    {"2", "DISK CONTROL", "DISK", ""},
    {"3", "KEYBOARD CONTROL", "KEYBD", ""},
    {"4", "HARMONIC PROFILES", "HARM", ""},
    {"5", "SYNTH", "SYNTH", ""},
    {"6", "WAVEFORM DRAWING", "WAVE", ""},
    {"7", "CONTROL PARAMETERS", "CTRL", ""},
    {"8", "SOUND SAMPLING", "SAMPL", ""},
    {"0", "SPECTRAL DRAWING", "SPECT", ""},
    {"C", "CHIP VOICES", "CHIP", ""},
    {"D", "WAVEFORM DISPLAY", "DISPL", ""},
    {"K", "VIRTUAL KEYBOARD", "VKEYS", ""},
    {"L", "LOOP LIBRARY", "LOOPS", ""},
    {"M", "MIXER", "MIXER", ""},
    {"R", "REAL-TIME COMPOSER", "COMPO", ""},
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
