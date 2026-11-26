#include <array>
#include <cstdio>
#include <string>

#include "core/text.h"
#include "gfx/palette.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

// Geometry test card covering the whole canvas. On a correctly set-up display the big circle is
// round on the physical panel, the outer frame touches all four bezel edges, and the overscan
// ticks show how much of each edge the bezel hides.
class TestcardPage final : public Page {
public:
    std::string_view id() const override { return "TESTCARD"; }
    std::string_view title() const override { return "TEST CARD"; }
    bool full_canvas() const override { return true; }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const int w = c.width();
        const int h = c.height();
        const int cx = w / 2;
        const int cy = h / 2;

        // 32 px grid, dotted.
        for (int x = 32; x < w; x += 32) {
            c.dotted_line(x, 0, x, h - 1, 2, gfx::kInkGrid);
        }
        for (int y = 32; y < h; y += 32) {
            c.dotted_line(0, y, w - 1, y, 2, gfx::kInkGrid);
        }

        // Overscan ticks: one every 1 % of the size per edge, longer every 5 %.
        for (int pct = 1; pct <= 10; ++pct) {
            const int len = pct % 5 == 0 ? 10 : 5;
            const int ox = w * pct / 100;
            const int oy = h * pct / 100;
            c.vline(ox, cy - len, cy + len, gfx::kInkText);
            c.vline(w - 1 - ox, cy - len, cy + len, gfx::kInkText);
            c.hline(cx - len, cx + len, oy, gfx::kInkText);
            c.hline(cx - len, cx + len, h - 1 - oy, gfx::kInkText);
        }

        c.rect(gfx::Rect{0, 0, w, h}, gfx::kInkBright);
        c.rect(gfx::Rect{2, 2, w - 4, h - 4}, gfx::kInkText);

        // Centre circle (round when pixels are square on the panel) and corner circles.
        const int r = h / 2 - 12;
        c.circle(cx, cy, r, gfx::kInkBright);
        c.circle(cx, cy, r / 2, gfx::kInkText);
        constexpr int kCorner = 36;
        c.circle(kCorner + 4, kCorner + 4, kCorner, gfx::kInkText);
        c.circle(w - kCorner - 5, kCorner + 4, kCorner, gfx::kInkText);
        c.circle(kCorner + 4, h - kCorner - 5, kCorner, gfx::kInkText);
        c.circle(w - kCorner - 5, h - kCorner - 5, kCorner, gfx::kInkText);
        c.hline(cx - r, cx + r, cy, gfx::kInkDim);
        c.vline(cx, cy - r, cy + r, gfx::kInkDim);

        // Palette bars across the middle.
        constexpr int kBarW = 12;
        const int bars_x = cx - gfx::kPaletteSize / 2 * kBarW;
        for (int i = 0; i < gfx::kPaletteSize; ++i) {
            c.fill_rect(gfx::Rect{bars_x + i * kBarW, cy + 20, kBarW, 20}, static_cast<std::uint8_t>(i));
        }
        c.rect(gfx::Rect{bars_x - 1, cy + 19, gfx::kPaletteSize * kBarW + 2, 22}, gfx::kInkText);

        // One-pixel resolution wedges: alternating lines get closer together.
        for (int i = 0; i < 6; ++i) {
            const int x = cx - 60 + i * 4 * (i + 1) / 2;
            c.vline(x, cy - 52, cy - 28, gfx::kInkText);
        }
        for (int x = cx + 20; x < cx + 60; x += 2) {
            c.vline(x, cy - 52, cy - 28, gfx::kInkText);
        }

        const DisplayStatus& d = ui.app().display();
        // Captions on a cleared band so the grid does not run through the letters.
        auto caption = [&](int row, std::string_view s, std::uint8_t ink) {
            const int len = static_cast<int>(s.size());
            const int x = (w - len * kCellW) / 2;
            c.fill_rect(gfx::Rect{x - 4, py(row), len * kCellW + 7, kCellH}, gfx::kInkBg);
            widgets::text_px(c, ui, x, py(row), s, ink);
        };
        char line[96];
        std::snprintf(line, sizeof line, "%dX%d CANVAS  %s", w, h, std::string(ui.theme().name).c_str());
        caption(9, "PHOSPHOR TEST CARD", gfx::kInkBright);
        caption(10, line, gfx::kInkText);
        std::snprintf(line, sizeof line, "%s  %dX%d  PAR %.3f", to_upper(d.monitor_name).substr(0, 24).c_str(),
                      d.mode_w, d.mode_h, d.pixel_aspect);
        caption(21, line, gfx::kInkText);
        caption(22, "ANY KEY RETURNS", gfx::kInkDim);
    }
};

} // namespace

std::unique_ptr<Page> make_testcard_page() { return std::make_unique<TestcardPage>(); }

} // namespace phos::ui
