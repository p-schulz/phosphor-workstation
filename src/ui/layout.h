#pragma once

#include "gfx/canvas.h"

// Screen geometry (see docs/screens.md §1). Everything in the UI is placed in cell coordinates
// plus pixel offsets, so changing the canvas or cell size only touches this file.
namespace phos::ui {

inline constexpr int kCanvasW = 512;
inline constexpr int kCanvasH = 384;
inline constexpr int kCellW = 8;
inline constexpr int kCellH = 12;
inline constexpr int kCols = kCanvasW / kCellW; // 64
inline constexpr int kRows = kCanvasH / kCellH; // 32

inline constexpr int kRowHeader = 0;
inline constexpr int kRowCommand = 1;
inline constexpr int kBodyTop = 2;
inline constexpr int kBodyRows = 27; // rows 2..28
inline constexpr int kRowSoftKeys = 29; // two rows: 29..30
inline constexpr int kRowInfo = 31;

inline constexpr int kBarCol = 8;   // header bar / command field start
inline constexpr int kBarCols = 44; // ..col 51
inline constexpr int kRightCol = 53; // right status block, 11 cols

constexpr int px(int col) { return col * kCellW; }
constexpr int py(int row) { return row * kCellH; }
constexpr gfx::Rect cells(int col, int row, int cols, int rows) {
    return gfx::Rect{col * kCellW, row * kCellH, cols * kCellW, rows * kCellH};
}

} // namespace phos::ui
