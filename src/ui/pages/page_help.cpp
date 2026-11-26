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

constexpr std::array<PageHelp, 8> kPageHelp = {{
    {"1", "PAGE LIST. POINT AT A LINE WITH THE PEN OR TYPE P<ID>.", "GREY PAGES ARE NOT BUILT YET."},
    {"S", "DISPLAY: SCALING, PANEL ASPECT, OVERSCAN, THEME, CRT LOOK (PER MONITOR).",
     "AUDIO: OUTPUT DEVICE, RATE, BUFFER, TEST TONE, MASTER. MIDI: PAGE 3."},
    {"2", "FILES: ARROWS MOVE, ENTER OPENS A FOLDER OR IMPORTS INTO THE CURRENT INSTRUMENT,",
     "A = AUDITION, BACKSPACE = PARENT FOLDER. WAV AIFF FLAC MP3 OGG."},
    {"7", "INSTRUMENT PARAMETERS: LEVEL, PITCH, INTERPOLATION, AMP AND FILTER ENVELOPES,",
     "FILTER, VELOCITY. ARROWS SELECT, LEFT/RIGHT CHANGE (SHIFT = BIGGER STEPS)."},
    {"8", "SAMPLE EDITOR: SELECTION, LOOP, TRIM, FADES, NORMALIZE, REVERSE, XFADE LOOP.",
     "CLICK THE WAVEFORM TO PLACE THE FOCUSED MARKER. EVERY EDIT CAN BE UNDONE."},
    {"3", "MIDI INPUTS (ON/OFF, CHANNEL), TIMING, VELOCITY, BEND, CC LEARN,",
     "LATENCY REPORT. INPUTS RECONNECT BY NAME WHEN PLUGGED IN AGAIN."},
    {"K", "COMPUTER KEYBOARD AS PIANO. KEYJAZZ ON, THEN PLAY; [ ] = OCTAVE.",
     "ESC TURNS KEYJAZZ OFF. MOD+K TOGGLES IT FROM ANY PAGE."},
    {"H", "THIS PAGE.", ""},
}};

constexpr std::array<std::pair<std::string_view, std::string_view>, 10> kKeys = {{
    {"F1-F12", "PAGES (SEE SOFT KEYS)"},
    {"ESC", "BACK TO THE COMMAND LINE"},
    {"\x82 / \x83", "COMMAND HISTORY"},
    {"TAB", "COMPLETE COMMAND"},
    {"MOD+K", "KEYJAZZ ON / OFF"},
    {"\x83 ON EMPTY", "ENTER PAGE FIELDS"},
    {"\x80 \x81 + -", "CHANGE FIELD (SHIFT X10)"},
    {"MOD+SHIFT+D", "NEXT MONITOR"},
    {"ALT+ENTER", "FULLSCREEN ON / OFF"},
    {"[ ]", "KEYJAZZ OCTAVE"},
}};

class HelpPage final : public Page {
    static constexpr int kLines = 17;
    static constexpr int kPerPage = 2 * kLines;
    static int pages() {
        int n = 0;
        for (const VerbInfo& v : verb_table()) {
            n += v.milestone.empty() ? 1 : 0;
        }
        return (n + kPerPage - 1) / kPerPage;
    }
    int page_ = 0;

public:
    std::string_view id() const override { return "H"; }
    std::string_view title() const override { return "HELP"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type != InputEvent::Type::Key) {
            return false;
        }
        if (ev.key == Key::Down || ev.key == Key::PageDown) {
            page_ = std::min(page_ + 1, pages() - 1);
            return true;
        }
        if (ev.key == Key::Up || ev.key == Key::PageUp) {
            if (page_ == 0) {
                ui.set_focus(Focus::Command);
            }
            page_ = std::max(page_ - 1, 0);
            return true;
        }
        return false;
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const std::string about(ui.previous_page_id());
        const PageInfo* info = find_page_info(about);
        const std::string heading = "HELP: PAGE " + about + (info ? " " + std::string(info->name) : std::string{});
        widgets::frame(c, ui, gfx::Rect{2, py(2) + 6, kCanvasW - 4, py(5) - 3 - (py(2) + 6)}, heading);
        std::string_view l1 = "NO HELP FOR THIS PAGE YET.";
        std::string_view l2;
        for (const auto& h : kPageHelp) {
            if (h.id == about) {
                l1 = h.line1;
                l2 = h.line2;
            }
        }
        widgets::small_text(c, ui, px(2), py(3) + 2, l1);
        widgets::small_text(c, ui, px(2), py(3) + 11, l2);

        // Two columns of usages in the small font, 10 px pitch, 17 lines each; LATER on the last line.
        widgets::frame(c, ui, gfx::Rect{2, py(5) + 6, kCanvasW - 4, py(22) - 3 - (py(5) + 6)}, "COMMANDS");
        int line = 0;
        std::string coming;
        for (const VerbInfo& v : verb_table()) {
            if (!v.milestone.empty()) {
                coming += (coming.empty() ? "" : " ") + std::string(v.verb);
                continue;
            }
            const int on_page = line - page_ * kPerPage;
            ++line;
            if (on_page < 0 || on_page >= kPerPage) {
                continue;
            }
            const int x = on_page < kLines ? px(2) : px(33);
            widgets::small_text(c, ui, x, py(6) - 2 + (on_page % kLines) * 10, fit(v.usage, 40), gfx::kInkText);
        }
        const std::string foot = pages() > 1 ? "PAGE " + std::to_string(page_ + 1) + "/" + std::to_string(pages()) +
                                                   " (DOWN = MORE)   LATER: " + coming
                                             : "LATER: " + coming;
        widgets::small_text(c, ui, px(2), py(6) - 2 + kLines * 10, foot, gfx::kInkGrid, 5);

        widgets::frame(c, ui, gfx::Rect{2, py(22) + 6, kCanvasW - 4, py(29) - 3 - (py(22) + 6)}, "KEYS");
        for (std::size_t i = 0; i < kKeys.size(); ++i) {
            const int col = i < 5 ? 2 : 33;
            const int r = 23 + static_cast<int>(i % 5);
            widgets::text(c, ui, col, r, kKeys[i].first);
            widgets::small_text(c, ui, px(col + 12), py(r) + 3, kKeys[i].second, gfx::kInkDim);
        }
    }
};

} // namespace

std::unique_ptr<Page> make_help_page() { return std::make_unique<HelpPage>(); }

} // namespace phos::ui
