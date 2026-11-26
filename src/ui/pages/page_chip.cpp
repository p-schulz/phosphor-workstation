// Page C — CHIP VOICES: the Game Boy view or the SNES view of the current instrument (docs/screens.md §3).

#include <cstdio>

#include "core/text.h"
#include "ui/pages/chip_views.h"
#include "ui/pages/pages.h"

namespace phos::ui {

namespace {

class ChipPage final : public Page {
public:
    std::string_view id() const override { return "C"; }
    std::string_view title() const override { return "CHIP VOICES"; }
    bool has_fields() const override { return true; }

    void update(const InputState& in, Ui& ui) override {
        switch (kind(ui)) {
        case model::InstrumentKind::Gb:
            gb_.update(in, ui);
            break;
        case model::InstrumentKind::Snes:
            snes_.update(in, ui);
            break;
        default:
            break;
        }
    }
    bool on_key(const InputEvent& ev, Ui& ui) override {
        switch (kind(ui)) {
        case model::InstrumentKind::Gb:
            return gb_.on_key(ev, ui);
        case model::InstrumentKind::Snes:
            return snes_.on_key(ev, ui);
        default:
            return false;
        }
    }
    void on_zone(int zone, int wheel, Ui& ui) override {
        if (kind(ui) == model::InstrumentKind::Gb) {
            gb_.on_zone(zone, wheel, ui);
        } else if (kind(ui) == model::InstrumentKind::Snes) {
            snes_.on_zone(zone, wheel, ui);
        }
    }
    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const model::Document& doc = ui.app().workspace().doc();
        const model::Instrument& inst = doc.current_instrument();
        char buf[64];
        std::snprintf(buf, sizeof buf, "INSTRUMENT %02d  %s  (%s)", doc.current + 1, fit(inst.name, 12).c_str(),
                      model::kind_name(inst.kind));
        widgets::text(c, ui, 1, 2, buf);
        if (inst.kind == model::InstrumentKind::Gb) {
            gb_.draw(c, ui);
        } else if (inst.kind == model::InstrumentKind::Snes) {
            snes_.draw(c, ui);
        } else {
            widgets::text(c, ui, 2, 5, "PAGE C EDITS CHIP INSTRUMENTS.", gfx::kInkDim);
            widgets::text(c, ui, 2, 7, "KIND GB: GAME BOY-STYLE PULSE / WAVE / NOISE.", gfx::kInkDim);
            widgets::text(c, ui, 2, 8, "KIND SNES: CONVERTS THE IMPORTED SAMPLE TO BRR.", gfx::kInkDim);
            widgets::text(c, ui, 2, 9, "OR INST <N> TO PICK ANOTHER INSTRUMENT.", gfx::kInkDim);
        }
    }

private:
    static model::InstrumentKind kind(const Ui& ui) { return ui.app().workspace().doc().current_instrument().kind; }
    chip::GbView gb_;
    chip::SnesView snes_;
};

} // namespace

std::unique_ptr<Page> make_chip_page() { return std::make_unique<ChipPage>(); }

} // namespace phos::ui
