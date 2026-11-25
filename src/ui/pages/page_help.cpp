#include <array>
#include <string>

#include "core/text.h"
#include "ui/commands.h"
#include "ui/layout.h"
#include "ui/page_catalog.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

struct PageHelp {
    std::string_view id;
    std::string_view line1;
    std::string_view line2;
};

constexpr std::array<PageHelp, 3> kPageHelp = {{
    {"1", "PAGE LIST. POINT AT A LINE WITH THE PEN OR TYPE P<ID>.", "GREY PAGES ARE NOT BUILT YET."},
    {"S", "DISPLAY SETUP FOR THE CURRENT MONITOR: SCALING, PANEL ASPECT,",
     "OVERSCAN, THEME AND CRT LOOK. AUDIO AND MIDI FOLLOW IN M1/M2."},
    {"H", "THIS PAGE.", ""},
}};

constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kKeys = {{
    {"F1-F12", "PAGES (SEE SOFT KEYS)"},
    {"ESC", "BACK TO THE COMMAND LINE"},
    {"\x82 / \x83", "COMMAND HISTORY"},
    {"TAB", "COMPLETE COMMAND"},
    {"\x83 ON EMPTY", "ENTER PAGE FIELDS"},
    {"\x80 \x81 + -", "CHANGE FIELD (SHIFT X10)"},
    {"MOD+SHIFT+D", "NEXT MONITOR"},
    {"ALT+ENTER", "FULLSCREEN ON / OFF"},
}};

class HelpPage final : public Page {
public:
    std::string_view id() const override { return "H"; }
    std::string_view title() const override { return "HELP"; }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const std::string about(ui.previous_page_id());
        const PageInfo* info = find_page_info(about);
        const std::string heading = "HELP: PAGE " + about + (info ? " " + std::string(info->name) : std::string{});
        widgets::frame(c, ui, gfx::Rect{2, py(2) + 6, kCanvasW - 4, py(6) - 3 - (py(2) + 6)}, heading);
        std::string_view l1 = "NO HELP FOR THIS PAGE YET.";
        std::string_view l2;
        for (const auto& h : kPageHelp) {
            if (h.id == about) {
                l1 = h.line1;
                l2 = h.line2;
            }
        }
        widgets::small_text(c, ui, px(2), py(3) + 4, l1);
        widgets::small_text(c, ui, px(2), py(4) + 4, l2);

        widgets::frame(c, ui, gfx::Rect{2, py(6) + 6, kCanvasW - 4, py(21) - 3 - (py(6) + 6)}, "COMMANDS");
        int row = 7;
        std::string coming;
        for (const VerbInfo& v : verb_table()) {
            if (!v.milestone.empty()) {
                coming += (coming.empty() ? "" : " ") + std::string(v.verb);
                continue;
            }
            widgets::text(c, ui, 2, row, fit(v.usage, 34));
            widgets::small_text(c, ui, px(37), py(row) + 3, v.summary, gfx::kInkDim);
            ++row;
        }
        widgets::small_text(c, ui, px(2), py(row) + 4, "LATER: " + coming, gfx::kInkGrid, 5);

        widgets::frame(c, ui, gfx::Rect{2, py(21) + 6, kCanvasW - 4, py(29) - 3 - (py(21) + 6)}, "KEYS");
        for (std::size_t i = 0; i < kKeys.size(); ++i) {
            const int col = i < 4 ? 2 : 33;
            const int r = 22 + static_cast<int>(i % 4);
            widgets::text(c, ui, col, r, kKeys[i].first);
            widgets::small_text(c, ui, px(col + 12), py(r) + 3, kKeys[i].second, gfx::kInkDim);
        }
    }
};

} // namespace

std::unique_ptr<Page> make_help_page() { return std::make_unique<HelpPage>(); }

} // namespace phos::ui
