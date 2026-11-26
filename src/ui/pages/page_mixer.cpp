// Page M — MIXER: 16 tracks, eight per screen (1-8 / A-H), each with a meter, a volume fader,
// pan, mute, solo, two inserts and sends A / B; master strip on the right with the master inserts
// and the two returns. The panel at the bottom edits the focused effect (docs/screens.md §3,
// PLAN.md §7.8).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/text.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

// Strip parameters top to bottom; the master strip uses kVolume and kIns1..kSendB (the last two
// are the returns A / B there). kFx0.. are the focused effect's parameters in the panel.
enum Param : int { kVolume, kPan, kMute, kSolo, kIns1, kIns2, kSendA, kSendB, kFx0, kParams = kFx0 + model::kFxParams };
constexpr int kStrips = 8;
constexpr int kMaster = kStrips; // strip index of the master
constexpr int kStripW = 56;
constexpr int kStripX0 = 8;
constexpr int kMeterTop = py(5);
constexpr int kMeterH = py(17) - py(5);
constexpr int kZoneBank = 900;
constexpr int kZoneFx = 1000;

float db_to_unit(float db) { return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f); } // -60..+6 dB
float peak_unit(float peak) {
    if (peak <= 0.0f) {
        return 0.0f;
    }
    return std::clamp((20.0f * std::log10(peak) + 60.0f) / 60.0f, 0.0f, 1.0f); // -60..0 dBFS
}

std::string pan_text(float pan) {
    const int p = static_cast<int>(std::lround(pan * 100.0f));
    if (p == 0) {
        return "  C";
    }
    char buf[8];
    std::snprintf(buf, sizeof buf, "%c%2d", p < 0 ? 'L' : 'R', std::abs(p));
    return buf;
}

std::string fx_short(model::FxType t) {
    return t == model::FxType::Off ? std::string("----") : std::string(model::fx_name(t)).substr(0, 5);
}

std::string param_text(const model::FxParamInfo& info, float v) {
    if (info.choices) {
        return info.choices[static_cast<int>(v - info.min)];
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, info.format, static_cast<double>(v));
    return buf;
}

class MixerPage final : public Page {
public:
    std::string_view id() const override { return "M"; }
    std::string_view title() const override { return "MIXER"; }
    bool has_fields() const override { return true; }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type == InputEvent::Type::Text) {
            if (ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_') {
                change(ui, (ev.ch == '+' || ev.ch == '=') ? 1 : -1, false);
                return true;
            }
            return false;
        }
        const bool big = (ev.mods & kModShift) != 0;
        const bool panel = param_ >= kFx0;
        switch (ev.key) {
        case Key::Left:
            if (panel) {
                param_ = std::max<int>(kFx0, param_ - 1);
            } else {
                strip_ = std::max(0, strip_ - 1);
                param_ = valid(param_);
                select_fx_row();
            }
            return true;
        case Key::Right:
            if (panel) {
                param_ = std::min(kFx0 + used_params(ui) - 1, param_ + 1);
            } else {
                strip_ = std::min(kMaster, strip_ + 1);
                param_ = valid(param_);
                select_fx_row();
            }
            return true;
        case Key::Up:
            if (panel) {
                param_ = kIns1 + (strip_ == kMaster ? fx_sel_ : std::min(fx_sel_, 1));
            } else if (param_ == kVolume) {
                ui.set_focus(Focus::Command);
            } else {
                param_ = strip_ == kMaster && param_ == kIns1 ? kVolume : param_ - 1;
                select_fx_row();
            }
            return true;
        case Key::Down:
            if (panel) {
                return true;
            }
            if (param_ == kSendB) {
                if (fx_type(ui) != model::FxType::Off) {
                    param_ = kFx0; // into the panel of the selected effect
                }
                return true;
            }
            param_ = strip_ == kMaster && param_ == kVolume ? kIns1 : param_ + 1;
            select_fx_row();
            return true;
        case Key::PageUp:
        case Key::PageDown:
            change(ui, ev.key == Key::PageUp ? 1 : -1, big);
            return true;
        case Key::Enter:
            if (param_ >= kIns1 && param_ <= kSendB && (strip_ == kMaster || param_ <= kIns2)) {
                fx_sel_ = param_ - kIns1;
                if (fx_type(ui) != model::FxType::Off) {
                    param_ = kFx0; // into the panel
                    return true;
                }
            }
            change(ui, 1, big);
            return true;
        case Key::Tab:
            bank_ = 1 - bank_;
            return true;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (zone == kZoneBank) {
            bank_ = 1 - bank_;
            return;
        }
        if (zone >= kZoneFx) {
            param_ = kFx0 + std::clamp(zone - kZoneFx, 0, model::kFxParams - 1);
            change(ui, wheel != 0 ? wheel : 1, false);
            return;
        }
        strip_ = std::clamp(zone / 16, 0, kMaster);
        param_ = std::clamp(zone % 16, 0, kFx0 - 1);
        if (param_ >= kIns1 && (strip_ == kMaster || param_ <= kIns2)) {
            fx_sel_ = param_ - kIns1;
            if (wheel == 0) {
                return; // a click selects the effect for the panel; the wheel changes the type
            }
        }
        change(ui, wheel != 0 ? wheel : 1, false);
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const bool fields = ui.focus() == Focus::Fields;
        const model::Document& doc = ui.app().workspace().doc();
        const model::Song& song = doc.song;
        const AudioInfo& audio = ui.app().audio();
        constexpr std::array<std::string_view, 2> kBanks = {"TRACKS 1-8", "TRACKS A-H"};
        widgets::option_group(c, ui, 1, 2, kBanks, bank_, false, kZoneBank);
        widgets::small_text(c, ui, px(30), py(2) + 3, "TAB = OTHER EIGHT  ENTER EDITS AN EFFECT", gfx::kInkDim);

        for (int s = 0; s <= kMaster; ++s) {
            const int x = kStripX0 + s * kStripW;
            const bool master = s == kMaster;
            const int track = bank_ * kStrips + s;
            const float peak = master ? std::max(audio.meters.peak_l, audio.meters.peak_r)
                                      : audio.bus_peaks[static_cast<std::size_t>(track)];
            const float vol = master ? song.master_db : song.tracks[static_cast<std::size_t>(track)].volume_db;
            widgets::small_text(c, ui, x, py(3) + 3, master ? std::string("MASTER") : strip_name(doc, track),
                                master ? gfx::kInkBright : gfx::kInkDim);
            // Meter (left) and fader (right).
            const gfx::Rect meter{x + 2, kMeterTop, 12, kMeterH};
            c.rect(meter, gfx::kInkGrid);
            draw_meter(c, meter, peak);
            const int fx = x + 30;
            c.vline(fx, kMeterTop, kMeterTop + kMeterH - 1, gfx::kInkDim);
            for (int db = 6; db >= -60; db -= 6) {
                const int y = kMeterTop + kMeterH - 1 - static_cast<int>(db_to_unit(static_cast<float>(db)) * static_cast<float>(kMeterH - 1));
                c.hline(fx - (db == 0 ? 5 : 3), fx + (db == 0 ? 5 : 3), y, db == 0 ? gfx::kInkText : gfx::kInkGrid);
            }
            const int ky = kMeterTop + kMeterH - 1 - static_cast<int>(db_to_unit(vol) * static_cast<float>(kMeterH - 1));
            const bool focused_vol = fields && strip_ == s && param_ == kVolume;
            c.fill_rect(gfx::Rect{fx - 8, ky - 2, 17, 5}, focused_vol ? gfx::kInkSelect : gfx::kInkText);
            ui.add_zone(gfx::Rect{x, kMeterTop, kStripW - 4, kMeterH}, ZoneOwner::Page, s * 16 + kVolume);

            char buf[16];
            std::snprintf(buf, sizeof buf, "%5.1f", static_cast<double>(vol));
            widgets::small_text(c, ui, x + 6, py(17) + 3, buf, focused_vol ? gfx::kInkBright : gfx::kInkText);
            const auto cell = [&](int param, int row, const std::string& text) {
                const bool f = fields && strip_ == s && param_ == param;
                const gfx::Rect r{x + 2, py(row) + 1, kStripW - 8, 11};
                if (f) {
                    c.fill_rect(r, gfx::kInkSelect);
                }
                widgets::small_text(c, ui, x + 4, py(row) + 3, text, f ? gfx::kInkBg : gfx::kInkText);
                ui.add_zone(r, ZoneOwner::Page, s * 16 + param);
            };
            if (master) {
                cell(kIns1, 21, "1 " + fx_short(song.master_fx[0].type));
                cell(kIns2, 22, "2 " + fx_short(song.master_fx[1].type));
                cell(kSendA, 23, "RA " + fx_short(song.returns[0].type));
                cell(kSendB, 24, "RB " + fx_short(song.returns[1].type));
                continue;
            }
            const auto& tr = song.tracks[static_cast<std::size_t>(track)];
            const bool fpan = fields && strip_ == s && param_ == kPan;
            const gfx::Rect pan_r{x + 4, py(18) + 2, 40, 10};
            if (fpan) {
                c.fill_rect(pan_r, gfx::kInkSelect);
            }
            widgets::small_text(c, ui, x + 10, py(18) + 3, pan_text(tr.pan), fpan ? gfx::kInkBg : gfx::kInkText);
            ui.add_zone(pan_r, ZoneOwner::Page, s * 16 + kPan);
            toggle(c, ui, gfx::Rect{x + 4, py(19) + 2, 18, 10}, "M", tr.mute, fields && strip_ == s && param_ == kMute, s * 16 + kMute, gfx::kInkWarn);
            toggle(c, ui, gfx::Rect{x + 26, py(19) + 2, 18, 10}, "S", tr.solo, fields && strip_ == s && param_ == kSolo, s * 16 + kSolo, gfx::kInkBright);
            widgets::text(c, ui, (x + 18) / kCellW, 20, std::string(1, model::track_label(track)),
                          track == song.current_track ? gfx::kInkBright : gfx::kInkDim);
            cell(kIns1, 21, "1 " + fx_short(tr.inserts[0].type));
            cell(kIns2, 22, "2 " + fx_short(tr.inserts[1].type));
            cell(kSendA, 23, "A " + std::to_string(static_cast<int>(std::lround(tr.send_a * 100.0f))));
            cell(kSendB, 24, "B " + std::to_string(static_cast<int>(std::lround(tr.send_b * 100.0f))));
        }
        draw_panel(c, ui, fields);
        widgets::small_text(c, ui, px(1), py(28) + 3,
                            "ARROWS MOVE, +/- CHANGE (SHIFT = BIG STEP), ENTER TOGGLES.  METERS: POST FADER",
                            gfx::kInkDim);
    }

private:
    // Post-fader peak: green up to -18 dBFS, yellow to -6, red above (single-hue themes draw it in
    // one ink); the whole bar turns to the warning ink near full scale (-1 dBFS).
    static void draw_meter(gfx::Canvas& c, gfx::Rect meter, float peak) {
        const int inner = meter.h - 2;
        const int mh = static_cast<int>(peak_unit(peak) * static_cast<float>(inner));
        if (mh <= 0) {
            return;
        }
        const int bottom = meter.y + meter.h - 1;
        if (peak >= 0.89f) {
            c.fill_rect(gfx::Rect{meter.x + 1, bottom - mh, meter.w - 2, mh}, gfx::kInkWarn);
            return;
        }
        const int low = static_cast<int>(42.0f / 60.0f * static_cast<float>(inner)); // -18 dBFS
        const int mid = static_cast<int>(54.0f / 60.0f * static_cast<float>(inner)); // -6 dBFS
        const auto band = [&](int from, int to, std::uint8_t ink) {
            const int top = std::min(to, mh);
            if (top > from) {
                c.fill_rect(gfx::Rect{meter.x + 1, bottom - top, meter.w - 2, top - from}, ink);
            }
        };
        band(0, low, gfx::kInkLevelLow);
        band(low, mid, gfx::kInkLevelMid);
        band(mid, inner, gfx::kInkLevelHigh);
    }

    static std::string strip_name(const model::Document& doc, int track) {
        const model::Song& song = doc.song;
        const auto& tr = song.tracks[static_cast<std::size_t>(track)];
        if (tr.type == model::TrackType::Loops) {
            return "LOOPS";
        }
        for (int i = 0; i < model::kLoopSlots; ++i) {
            const model::LoopSlot& l = song.loops[static_cast<std::size_t>(i)];
            if (l.sample && l.track == track) {
                return "L" + std::to_string(i + 1) + " " + fit(l.name, 5);
            }
        }
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02d %s", tr.instrument + 1,
                      fit(doc.instruments[static_cast<std::size_t>(tr.instrument)].name, 5).c_str());
        return buf;
    }

    // The effect the panel edits: track insert, master insert, or return.
    const model::FxSlot* fx_slot(const model::Song& s, std::string* title = nullptr) const {
        if (strip_ == kMaster) {
            if (title) {
                *title = fx_sel_ < 2 ? "MASTER INSERT " + std::to_string(fx_sel_ + 1)
                                     : std::string("RETURN ") + static_cast<char>('A' + fx_sel_ - 2);
            }
            return fx_sel_ < 2 ? &s.master_fx[static_cast<std::size_t>(fx_sel_)]
                               : &s.returns[static_cast<std::size_t>(fx_sel_ - 2)];
        }
        const int track = bank_ * kStrips + strip_;
        if (title) {
            *title = std::string("TRACK ") + model::track_label(track) + " INSERT " + std::to_string(std::min(fx_sel_, 1) + 1);
        }
        return &s.tracks[static_cast<std::size_t>(track)].inserts[static_cast<std::size_t>(std::min(fx_sel_, 1))];
    }
    model::FxSlot* fx_slot(model::Song& s) { return const_cast<model::FxSlot*>(std::as_const(*this).fx_slot(s)); }
    model::FxType fx_type(const Ui& ui) const { return fx_slot(ui.app().workspace().doc().song)->type; }
    bool is_return() const { return strip_ == kMaster && fx_sel_ >= 2; }
    int used_params(const Ui& ui) const {
        const model::FxType t = fx_type(ui);
        int n = 0;
        for (int i = 0; i < model::kFxParams; ++i) {
            n = *model::fx_param(t, i).name || (is_return() && i == model::kFxParams - 1) ? i + 1 : n;
        }
        return std::max(1, n);
    }
    // An effect row with focus is the one the panel edits.
    void select_fx_row() {
        if (param_ >= kIns1 && param_ < kFx0 && (strip_ == kMaster || param_ <= kIns2)) {
            fx_sel_ = param_ - kIns1;
        }
    }
    int valid(int param) const {
        if (param >= kFx0) {
            return param;
        }
        if (strip_ == kMaster) {
            return param == kVolume ? kVolume : std::max<int>(kIns1, param);
        }
        return param;
    }

    void draw_panel(gfx::Canvas& c, const Ui& ui, bool fields) const {
        const model::Song& song = ui.app().workspace().doc().song;
        std::string title;
        const model::FxSlot* slot = fx_slot(song, &title);
        title += ": ";
        title += model::fx_name(slot->type);
        widgets::frame(c, ui, gfx::Rect{2, py(25) + 6, kCanvasW - 4, py(28) - 2 - (py(25) + 6)}, title);
        if (slot->type == model::FxType::Off) {
            widgets::small_text(c, ui, px(2), py(26) + 4, "OFF. +/- ON THE STRIP PICKS AN EFFECT (CHORUS TAPE CRUSH LOFI PLATE DELAY DUCK)",
                                gfx::kInkDim);
            return;
        }
        int col = 2;
        for (int i = 0; i < model::kFxParams; ++i) {
            const model::FxParamInfo& info = model::fx_param(slot->type, i);
            const bool level = is_return() && i == model::kFxParams - 1; // a return's MIX is its level
            if (!*info.name && !level) {
                continue;
            }
            const std::string name = level ? "LEVEL" : info.name;
            const std::string value =
                level ? param_text(model::FxParamInfo{"LEVEL", 0, 1, 1, 0.05f, "%.2f"},
                                   song.return_level[static_cast<std::size_t>(fx_sel_ - 2)])
                      : param_text(info, slot->p[static_cast<std::size_t>(i)]);
            widgets::small_text(c, ui, px(col), py(26) + 4, name, gfx::kInkDim);
            const int vcol = col + (static_cast<int>(name.size()) * 6 + 13) / kCellW;
            widgets::value_field(c, ui, vcol, 26, 5, value, fields && param_ == kFx0 + i, kZoneFx + i);
            col = vcol + 7;
        }
    }

    static void toggle(gfx::Canvas& c, const Ui& ui, gfx::Rect r, std::string_view label, bool on, bool focused, int zone,
                       std::uint8_t on_ink) {
        if (on) {
            c.fill_rect(r, on_ink);
        }
        c.rect(r, focused ? gfx::kInkBright : gfx::kInkDim);
        widgets::small_text(c, ui, r.x + 6, r.y + 1, label, on ? gfx::kInkBg : gfx::kInkText);
        ui.add_zone(r, ZoneOwner::Page, zone);
    }

    void change(Ui& ui, int dir, bool big) {
        model::Workspace& ws = ui.app().workspace();
        model::Song s = ws.doc().song;
        const double now = ui.app().time_s();
        if (param_ >= kFx0) {
            change_fx(ws, s, param_ - kFx0, dir, big, now);
            return;
        }
        if (param_ >= kIns1 && (strip_ == kMaster || param_ <= kIns2)) {
            fx_sel_ = param_ - kIns1;
            model::FxSlot* slot = fx_slot(s);
            const auto t = static_cast<model::FxType>((static_cast<int>(slot->type) + model::kFxTypes + dir) % model::kFxTypes);
            *slot = model::make_fx(t);
            if (is_return()) {
                slot->p[model::kFxParams - 1] = 1.0f;
            }
            ws.set_song(s, "FX TYPE");
            return;
        }
        if (strip_ == kMaster) {
            s.master_db = std::clamp(s.master_db + static_cast<float>(dir) * (big ? 6.0f : 0.5f), -60.0f, 6.0f);
            ws.set_song(s, "MASTER", now);
            return;
        }
        auto& tr = s.tracks[static_cast<std::size_t>(bank_ * kStrips + strip_)];
        switch (param_) {
        case kVolume:
            tr.volume_db = std::clamp(tr.volume_db + static_cast<float>(dir) * (big ? 6.0f : 0.5f), -60.0f, 6.0f);
            ws.set_song(s, "VOLUME", now);
            break;
        case kPan:
            tr.pan = std::clamp(tr.pan + static_cast<float>(dir) * (big ? 0.25f : 0.05f), -1.0f, 1.0f);
            tr.pan = std::round(tr.pan * 100.0f) / 100.0f;
            ws.set_song(s, "PAN", now);
            break;
        case kMute:
            tr.mute = !tr.mute;
            ws.set_song(s, "MUTE");
            break;
        case kSolo:
            tr.solo = !tr.solo;
            ws.set_song(s, "SOLO");
            break;
        case kSendA:
        case kSendB: {
            float& send = param_ == kSendA ? tr.send_a : tr.send_b;
            send = std::clamp(std::round((send + static_cast<float>(dir) * (big ? 0.25f : 0.05f)) * 100.0f) / 100.0f, 0.0f, 1.0f);
            ws.set_song(s, "SEND", now);
            break;
        }
        default:
            break;
        }
    }

    void change_fx(model::Workspace& ws, model::Song& s, int i, int dir, bool big, double now) {
        model::FxSlot* slot = fx_slot(s);
        if (is_return() && i == model::kFxParams - 1) {
            float& lv = s.return_level[static_cast<std::size_t>(fx_sel_ - 2)];
            lv = std::clamp(std::round((lv + static_cast<float>(dir) * (big ? 0.25f : 0.05f)) * 100.0f) / 100.0f, 0.0f, 1.0f);
            ws.set_song(s, "RETURN LEVEL", now);
            return;
        }
        const model::FxParamInfo& info = model::fx_param(slot->type, i);
        if (!*info.name) {
            return;
        }
        float& v = slot->p[static_cast<std::size_t>(i)];
        if (info.choices) {
            const int n = static_cast<int>(info.max - info.min) + 1;
            v = info.min + static_cast<float>(((static_cast<int>(v - info.min) + dir) % n + n) % n);
        } else {
            v = std::clamp(v + static_cast<float>(dir) * info.step * (big ? 10.0f : 1.0f), info.min, info.max);
            v = std::round(v / info.step) * info.step;
        }
        model::clamp_fx(*slot);
        ws.set_song(s, std::string("FX ") + info.name, now);
    }

    int bank_ = 0;
    int strip_ = 0;
    int param_ = kVolume;
    int fx_sel_ = 0; // 0, 1: insert (master insert); 2, 3: return A / B (master strip)
};

} // namespace

std::unique_ptr<Page> make_mixer_page() { return std::make_unique<MixerPage>(); }

} // namespace phos::ui
