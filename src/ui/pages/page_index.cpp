#include <array>
#include <cmath>
#include <string>

#include "core/version.h"
#include "gfx/font.h"
#include "ui/layout.h"
#include "ui/page_catalog.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

using gfx::Point;
using gfx::Rect;

constexpr int kListCol = 26;
constexpr int kListRow = 4;
constexpr int kListWidth = 30;

// Original PHOSPHOR mark: a small monitor whose screen shows a decaying trace.
void draw_logo(gfx::Canvas& c, const Ui& ui) {
    constexpr int x0 = 28;
    constexpr int y0 = 52;
    constexpr int w = 136;
    constexpr int h = 104;
    constexpr int ch = 6; // chamfer
    const std::array<Point, 9> outline = {{{x0 + ch, y0},
                                           {x0 + w - ch, y0},
                                           {x0 + w, y0 + ch},
                                           {x0 + w, y0 + h - ch},
                                           {x0 + w - ch, y0 + h},
                                           {x0 + ch, y0 + h},
                                           {x0, y0 + h - ch},
                                           {x0, y0 + ch},
                                           {x0 + ch, y0}}};
    c.polyline(outline, gfx::kInkText);

    const Rect screen{x0 + 10, y0 + 9, w - 20, h - 26};
    c.rect(screen, gfx::kInkDim);
    for (int y = screen.y + 3; y < screen.y + screen.h - 2; y += 4) {
        c.dotted_line(screen.x + 3, y, screen.x + screen.w - 4, y, 4, gfx::kInkGrid);
    }
    // Decaying sine across the screen: brightest at the start, like a fresh phosphor trace.
    std::array<Point, 112> trace{};
    const int mid = screen.y + screen.h / 2;
    for (int i = 0; i < static_cast<int>(trace.size()); ++i) {
        const double t = i / static_cast<double>(trace.size() - 1);
        const double amp = (screen.h / 2.0 - 5.0) * std::exp(-2.2 * t);
        trace[static_cast<std::size_t>(i)] = Point{screen.x + 4 + i, mid - static_cast<int>(std::lround(amp * std::sin(t * 6.283185307 * 5.0)))};
    }
    c.polyline(std::span<const Point>(trace).subspan(0, 56), gfx::kInkBright);
    c.polyline(std::span<const Point>(trace).subspan(55), gfx::kInkText);

    // Control strip under the screen: two knobs and a power LED.
    const int strip_y = y0 + h - 10;
    c.circle(x0 + 20, strip_y, 3, gfx::kInkText);
    c.circle(x0 + 34, strip_y, 3, gfx::kInkText);
    c.fill_circle(x0 + w - 16, strip_y, 2, gfx::kInkBright);
    // Stand.
    c.line(x0 + 52, y0 + h, x0 + 44, y0 + h + 12, gfx::kInkText);
    c.line(x0 + w - 52, y0 + h, x0 + w - 44, y0 + h + 12, gfx::kInkText);
    c.hline(x0 + 30, x0 + w - 30, y0 + h + 12, gfx::kInkText);

    const std::string_view name = "P H O S P H O R";
    const int name_x = 8 + (176 - static_cast<int>(name.size()) * kCellW) / 2;
    widgets::text_px(c, ui, name_x, 196, name, gfx::kInkBright);
    const std::string_view sub = "RETRO MIDI WORKSTATION";
    const int sub_x = 8 + (176 - static_cast<int>(sub.size()) * 6) / 2;
    widgets::small_text(c, ui, sub_x, 216, sub, gfx::kInkText);
    const std::string ver = "V" + std::string(version_string());
    widgets::text_px(c, ui, 8 + (176 - static_cast<int>(ver.size()) * kCellW) / 2, 236, ver, gfx::kInkDim);
}

class IndexPage final : public Page {
public:
    std::string_view id() const override { return "1"; }
    std::string_view title() const override { return "INDEX"; }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (wheel != 0 || zone < 0 || zone >= static_cast<int>(kPageCatalog.size())) {
            return;
        }
        ui.go_to_page(kPageCatalog[static_cast<std::size_t>(zone)].id);
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        draw_logo(c, ui);
        c.vline(px(24) - 4, py(3), py(27), gfx::kInkGrid);

        for (std::size_t i = 0; i < kPageCatalog.size(); ++i) {
            const PageInfo& p = kPageCatalog[i];
            const int row = kListRow + static_cast<int>(i);
            const int zone = static_cast<int>(i);
            const bool available = p.milestone.empty();
            const bool hot = ui.hovered(ZoneOwner::Page, zone);
            const Rect r = cells(kListCol, row, kListWidth, 1);
            std::string line = "PAGE " + std::string(p.id) + "... " + std::string(p.name);
            std::uint8_t ink = available ? gfx::kInkText : gfx::kInkDim;
            if (hot) {
                c.fill_rect(Rect{r.x - 2, r.y, r.w + 4, r.h}, gfx::kInkBright);
                ink = gfx::kInkBg;
            }
            widgets::text(c, ui, kListCol, row, line, ink);
            if (!available) {
                widgets::small_text(c, ui, px(kListCol + kListWidth + 1), py(row) + 3, p.milestone,
                                    hot ? gfx::kInkText : gfx::kInkGrid);
            }
            ui.add_zone(r, ZoneOwner::Page, zone);
        }

        const int info_row = kListRow + static_cast<int>(kPageCatalog.size()) + 2;
        widgets::text(c, ui, kListCol, info_row, "PROJECT :");
        widgets::value_field(c, ui, kListCol + 10, info_row, 20, " " + ui.app().status().project, false, 100);
        widgets::small_text(c, ui, px(kListCol), py(info_row + 2) + 2, "TYPE P<ID>, PRESS F1-F12 OR POINT", gfx::kInkDim);
        widgets::small_text(c, ui, px(kListCol), py(info_row + 3) + 2, "WITH THE LIGHT PEN. GREY PAGES ARRIVE", gfx::kInkDim);
        widgets::small_text(c, ui, px(kListCol), py(info_row + 4) + 2, "IN THE MILESTONE SHOWN.", gfx::kInkDim);
    }
};

} // namespace

std::unique_ptr<Page> make_index_page() { return std::make_unique<IndexPage>(); }

} // namespace phos::ui
