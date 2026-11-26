#include "ui/widgets.h"

#include "ui/layout.h"

namespace phos::ui::widgets {

using gfx::Rect;

void text(gfx::Canvas& c, const Ui& ui, int col, int row, std::string_view s, std::uint8_t ink) {
    gfx::draw_text(c, ui.fonts().large, px(col), py(row), s, ink);
}

void text_px(gfx::Canvas& c, const Ui& ui, int x, int y, std::string_view s, std::uint8_t ink) {
    gfx::draw_text(c, ui.fonts().large, x, y, s, ink);
}

void small_text(gfx::Canvas& c, const Ui& ui, int x, int y, std::string_view s, std::uint8_t ink, int advance) {
    gfx::draw_text(c, ui.fonts().small, x, y, s, ink, advance);
}

void centered_text(gfx::Canvas& c, const Ui& ui, int col, int width, int row, std::string_view s, std::uint8_t ink) {
    const int len = static_cast<int>(s.size());
    const int x = px(col) + (px(width) - len * kCellW) / 2;
    gfx::draw_text(c, ui.fonts().large, x, py(row), s, ink);
}

void title_tab(gfx::Canvas& c, const Ui& ui, int x, int y, std::string_view s) {
    const int w = static_cast<int>(s.size()) * kCellW + 4;
    c.fill_rect(Rect{x - 2, y, w, kCellH}, gfx::kInkTab);
    gfx::draw_text(c, ui.fonts().large, x, y, s, gfx::kInkBg);
}

void frame(gfx::Canvas& c, const Ui& ui, Rect r, std::string_view title, std::uint8_t ink) {
    c.rect(r, ink);
    if (!title.empty()) {
        const int tw = static_cast<int>(title.size()) * kCellW;
        const int tx = r.x + (r.w - tw) / 2;
        // Tab straddles the frame's top edge, like the reference's "CONTROL PARAMETERS" header.
        title_tab(c, ui, tx, r.y - kCellH / 2, title);
    }
}

void label(gfx::Canvas& c, const Ui& ui, int col, int row, std::string_view s, int label_cols) {
    text(c, ui, col, row, s, gfx::kInkLabel);
    text(c, ui, col + label_cols, row, "=", gfx::kInkLabel);
}

void value_field(gfx::Canvas& c, const Ui& ui, int col, int row, int width, std::string_view value, bool focused,
                 int zone) {
    const Rect r = cells(col, row, width, 1);
    c.fill_rect(r, focused ? gfx::kInkSelect : gfx::kInkField);
    gfx::draw_text(c, ui.fonts().large, px(col), py(row), value.substr(0, static_cast<std::size_t>(width)),
                   focused ? gfx::kInkBg : gfx::kInkText);
    if (ui.hovered(ZoneOwner::Page, zone) && !focused) {
        c.rect(r, gfx::kInkCursor);
    }
    ui.add_zone(r, ZoneOwner::Page, zone);
}

int option_group(gfx::Canvas& c, const Ui& ui, int col, int row, std::span<const std::string_view> options,
                 int selected, bool focused, int zone_base) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        const auto& opt = options[i];
        const int w = static_cast<int>(opt.size());
        const Rect r = cells(col, row, w, 1);
        const int zone = zone_base + static_cast<int>(i);
        if (static_cast<int>(i) == selected) {
            c.fill_rect(Rect{r.x - 1, r.y, r.w + 1, r.h}, gfx::kInkSelect);
            gfx::draw_text(c, ui.fonts().large, r.x, r.y, opt, gfx::kInkBg);
            if (focused) {
                c.rect(Rect{r.x - 2, r.y - 1, r.w + 3, r.h + 2}, gfx::kInkAccent);
            }
        } else {
            gfx::draw_text(c, ui.fonts().large, r.x, r.y, opt, gfx::kInkText);
        }
        if (ui.hovered(ZoneOwner::Page, zone) && static_cast<int>(i) != selected) {
            c.hline(r.x, r.x + r.w - 2, r.y + r.h - 1, gfx::kInkCursor);
        }
        ui.add_zone(r, ZoneOwner::Page, zone);
        col += w + 1;
    }
    return col;
}

void button(gfx::Canvas& c, const Ui& ui, int col, int row, std::string_view caption, bool focused, int zone) {
    const Rect r = cells(col, row, static_cast<int>(caption.size()), 1);
    const Rect box{r.x - 3, r.y - 1, r.w + 5, r.h + 1};
    const bool hot = focused || ui.hovered(ZoneOwner::Page, zone);
    if (hot) {
        c.fill_rect(box, gfx::kInkSelect);
        gfx::draw_text(c, ui.fonts().large, r.x, r.y, caption, gfx::kInkBg);
    } else {
        c.rect(box, gfx::kInkText);
        gfx::draw_text(c, ui.fonts().large, r.x, r.y, caption, gfx::kInkText);
    }
    ui.add_zone(box, ZoneOwner::Page, zone);
}

} // namespace phos::ui::widgets
