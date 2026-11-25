#pragma once

#include <string>

#include "gfx/canvas.h"

namespace phos::ui {
class Ui;
}

// Screen chrome shared by every page (docs/screens.md §1):
//   row 0      INDEX | *** PAGE x READY *** | tempo + signature
//   row 1      COMMAND: <command line>      | position
//   rows 29-30 soft-key bar (F1..F12, P, S)
//   row 31     INFO: message                | project, CPU, voices, MIDI LED
namespace phos::ui::chrome {

inline constexpr int kZoneIndex = 1;
inline constexpr int kZoneCommand = 2;
inline constexpr int kZoneSoftKey = 100; // + 0..13
inline constexpr int kSoftKeyCount = 14; // F1..F12, P (back), S (setup)

void draw(gfx::Canvas& canvas, const Ui& ui);

// Command run by soft key `index` (0..11 = F1..F12, 12 = P, 13 = S). Empty = unassigned.
std::string soft_key_command(const Ui& ui, int index);

} // namespace phos::ui::chrome
