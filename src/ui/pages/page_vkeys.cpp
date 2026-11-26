// Page K — VIRTUAL KEYBOARD: KEYJAZZ on/off, a piano strip lighting every held note (KEYJAZZ and
// MIDI input), the key legend and the octave (docs/screens.md §3).

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "gfx/palette.h"
#include "ui/keyjazz.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

enum Field : int { kToggle, kOctDown, kOctUp, kFieldCount };
constexpr int zone_of(int field, int option = 0) { return field * 16 + option; }
constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};

// Piano strip: five octaves C2..B6 (MIDI 36..95).
constexpr int kFirstNote = 36;
constexpr int kOctaves = 5;
constexpr int kWhiteW = 14;
constexpr std::array<int, 7> kWhiteSemis = {0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 5> kBlackSemis = {1, 3, 6, 8, 10};
constexpr std::array<int, 5> kBlackAfterWhite = {0, 1, 3, 4, 5}; // black key sits right of this white key

bool held(const MidiInfo& mi, int note) { return note >= 0 && note < 128 && mi.meters.held_note(note); }

class VirtualKeyboardPage final : public Page {
public:
    std::string_view id() const override { return "K"; }
    std::string_view title() const override { return "VIRTUAL KEYBOARD"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type != InputEvent::Type::Key) {
            return false;
        }
        switch (ev.key) {
        case Key::Up:
            if (focus_ == 0) {
                ui.set_focus(Focus::Command);
            } else {
                --focus_;
            }
            return true;
        case Key::Down:
        case Key::Tab:
            focus_ = std::min(focus_ + 1, kFieldCount - 1);
            return true;
        case Key::Left:
        case Key::Right:
        case Key::Enter:
            activate(ui, ev.key == Key::Left ? 0 : 1);
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        focus_ = std::clamp(zone / 16, 0, kFieldCount - 1);
        if (wheel != 0) {
            ui.keyjazz_octave(wheel > 0 ? 1 : -1);
            return;
        }
        activate(ui, zone % 16);
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const bool fields = ui.focus() == Focus::Fields;
        const MidiInfo& mi = ui.app().midi();
        const int octave = ui.app().midi_config().keyjazz_octave;

        widgets::centered_text(c, ui, 0, kCols, 2, "VIRTUAL KEYBOARD");
        c.rect(gfx::Rect{4, py(3) + 1, kCanvasW - 8, kCellH + 2}, gfx::kInkText);
        widgets::text(c, ui, 2, 3, "KEYJAZZ: THE COMPUTER KEYBOARD PLAYS NOTES  :");
        widgets::option_group(c, ui, 50, 3, kOffOn, ui.keyjazz() ? 1 : 0, fields && focus_ == kToggle,
                              zone_of(kToggle));

        draw_piano(c, mi, octave);

        widgets::text(c, ui, 2, 13, "WHEN ON, THESE KEYS PLAY:");
        char buf[40];
        std::snprintf(buf, sizeof buf, "C%d - E%d", octave, octave + 1);
        widgets::small_text(c, ui, px(2), py(14) + 4, buf, gfx::kInkDim);
        std::snprintf(buf, sizeof buf, "C%d - E%d", octave + 1, octave + 2);
        widgets::small_text(c, ui, px(29), py(14) + 4, buf, gfx::kInkDim);
        // Lower row (base octave) and upper row (one octave up), tracker layout.
        draw_legend(c, ui, mi, octave, px(2), "ZXCV BNM,./", "SD GHJ L;");
        draw_legend(c, ui, mi, octave, px(29), "QWER TYUIOP", "23 567 90");

        widgets::text(c, ui, 2, 21, "OCTAVE");
        std::snprintf(buf, sizeof buf, "%d   (C%d = MIDI %d)", octave, octave, (octave + 1) * 12);
        widgets::text(c, ui, 9, 21, buf, gfx::kInkText);
        widgets::button(c, ui, 34, 21, "[ DOWN", fields && focus_ == kOctDown, zone_of(kOctDown));
        widgets::button(c, ui, 44, 21, "] UP", fields && focus_ == kOctUp, zone_of(kOctUp));

        widgets::small_text(c, ui, px(2), py(24) + 2, "ESC TURNS KEYJAZZ OFF. MOD+K TOGGLES IT ON ANY PAGE.", gfx::kInkDim);
        widgets::small_text(c, ui, px(2), py(25) + 2, "KEYS ARE PHYSICAL POSITIONS (US NAMES): ON QWERTZ THE Z NOTE IS THE Y KEY.",
                            gfx::kInkDim);
        widgets::small_text(c, ui, px(2), py(26) + 2, "THE STRIP ALSO SHOWS NOTES FROM MIDI INPUTS (PAGE 3).", gfx::kInkDim);
    }

private:
    void activate(Ui& ui, int option) {
        if (focus_ == kToggle) {
            ui.execute(option == 1 ? "KEYJAZZ ON" : "KEYJAZZ OFF");
        } else {
            ui.keyjazz_octave(focus_ == kOctUp ? 1 : -1);
        }
    }

    static void draw_piano(gfx::Canvas& c, const MidiInfo& mi, int octave) {
        const int whites = kOctaves * 7;
        const int x0 = (kCanvasW - whites * kWhiteW) / 2;
        const int y0 = py(5);
        const int h = py(11) - y0;
        const int bh = h * 6 / 10;
        // KEYJAZZ range marker under the strip (two octaves and a third from C<octave>).
        const int jazz_lo = (octave + 1) * 12;
        for (int o = 0; o < kOctaves; ++o) {
            for (int w = 0; w < 7; ++w) {
                const int note = kFirstNote + o * 12 + kWhiteSemis[static_cast<std::size_t>(w)];
                const gfx::Rect r{x0 + (o * 7 + w) * kWhiteW, y0, kWhiteW - 1, h};
                c.fill_rect(r, held(mi, note) ? gfx::kInkDim : gfx::kInkText);
                if (note >= jazz_lo && note <= jazz_lo + 28) {
                    c.hline(r.x, r.x + r.w - 1, y0 + h + 2, gfx::kInkBright);
                }
            }
            for (int b = 0; b < 5; ++b) {
                const int note = kFirstNote + o * 12 + kBlackSemis[static_cast<std::size_t>(b)];
                const int wx = x0 + (o * 7 + kBlackAfterWhite[static_cast<std::size_t>(b)] + 1) * kWhiteW;
                const gfx::Rect r{wx - 5, y0, 9, bh};
                c.fill_rect(r, held(mi, note) ? gfx::kInkDim : gfx::kInkBg);
                c.rect(r, gfx::kInkBg);
            }
        }
    }

    // Two rows of key caps like the reference: black keys above, offset half a key.
    static void draw_legend(gfx::Canvas& c, const Ui& ui, const MidiInfo& mi, int octave, int x0,
                            std::string_view whites, std::string_view blacks) {
        constexpr int kStep = 18;
        constexpr int kCapW = 16;
        constexpr int kCapH = 18;
        const int y_black = py(15) + 2;
        const int y_white = y_black + kCapH + 3;
        auto cap = [&](int x, int y, char key) {
            const int n = keyjazz::note(key, octave);
            const bool on = held(mi, n);
            const gfx::Rect r{x, y, kCapW, kCapH};
            if (on) {
                c.fill_rect(r, gfx::kInkPlay);
            } else {
                c.rect(r, gfx::kInkText);
            }
            widgets::text_px(c, ui, x + 4, y + 3, std::string_view(&key, 1), on ? gfx::kInkBg : gfx::kInkText);
        };
        // Spaces in the strings mark the gaps between key groups (E-F and B-C have no black key).
        int wi = 0;
        for (const char k : whites) {
            if (k != ' ') {
                cap(x0 + wi * kStep, y_white, k);
                ++wi;
            }
        }
        static constexpr std::array<int, 7> kBlackSlots = {0, 1, 3, 4, 5, 7, 8};
        int bi = 0;
        for (const char k : blacks) {
            if (k == ' ') {
                continue;
            }
            cap(x0 + kBlackSlots[static_cast<std::size_t>(bi)] * kStep + kStep / 2, y_black, k);
            ++bi;
        }
    }

    int focus_ = kToggle;
};

} // namespace

std::unique_ptr<Page> make_vkeys_page() { return std::make_unique<VirtualKeyboardPage>(); }

} // namespace phos::ui
