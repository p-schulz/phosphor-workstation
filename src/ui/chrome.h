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

// Rolling INFO line: pixel offset of a `text_px` wide message in a `box_px` wide field,
// `elapsed_s` after it appeared. Fits: 0. Longer: holds the start, scrolls left at kRollPxPerS
// until the end shows, holds the end, starts over.
inline constexpr double kRollHoldS = 1.5;
inline constexpr double kRollPxPerS = 48.0; // 6 characters a second
int roll_offset(int text_px, int box_px, double elapsed_s);

// Command run by soft key `index` (0..11 = F1..F12, 12 = P, 13 = S). Empty = unassigned.
std::string soft_key_command(const Ui& ui, int index);

} // namespace phos::ui::chrome
