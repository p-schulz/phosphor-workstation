#include "ui/chrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/text.h"
#include "gfx/font.h"
#include "ui/commands.h"
#include "ui/layout.h"
#include "ui/page_catalog.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui::chrome {

namespace {

using gfx::Rect;

constexpr int kSoftKeyW = 34;
constexpr int kSoftKeyGap = 2;
constexpr int kSoftKeyX0 = (kCanvasW - (kSoftKeyCount * kSoftKeyW + (kSoftKeyCount - 1) * kSoftKeyGap)) / 2;

Rect soft_key_rect(int i) {
    return Rect{kSoftKeyX0 + i * (kSoftKeyW + kSoftKeyGap), py(kRowSoftKeys), kSoftKeyW, 2 * kCellH};
}

std::string caption_for(const std::string& command) {
    if (command.empty()) {
        return {};
    }
    const auto cmd = parse_command(command);
    if (!cmd) {
        return {};
    }
    const std::string page = page_id_from_verb(cmd->verb);
    if (!page.empty()) {
        if (const PageInfo* info = find_page_info(page)) {
            return std::string(info->short_name);
        }
    }
    return fit(cmd->verb, 5);
}

bool command_available(const std::string& command) {
    const auto cmd = parse_command(command);
    if (!cmd) {
        return false;
    }
    const std::string page = page_id_from_verb(cmd->verb);
    if (!page.empty()) {
        const PageInfo* info = find_page_info(page);
        return info && info->milestone.empty();
    }
    const VerbInfo* v = find_verb(cmd->verb);
    return v && v->milestone.empty();
}

void draw_header(gfx::Canvas& c, const Ui& ui) {
    const bool hot = ui.hovered(ZoneOwner::Chrome, kZoneIndex);
    const Rect index_rect = cells(0, kRowHeader, 5, 1);
    if (hot) {
        c.fill_rect(Rect{index_rect.x, index_rect.y, index_rect.w + 1, index_rect.h}, gfx::kInkBright);
    }
    widgets::text(c, ui, 0, kRowHeader, "INDEX", hot ? gfx::kInkBg : gfx::kInkText);
    ui.add_zone(index_rect, ZoneOwner::Chrome, kZoneIndex);

    c.fill_rect(cells(kBarCol, kRowHeader, kBarCols, 1), gfx::kInkBright);
    const std::string banner = "***  PAGE " + std::string(ui.current_page_id()) + " READY  ***";
    widgets::centered_text(c, ui, kBarCol, kBarCols, kRowHeader, banner, gfx::kInkBg);

    const StatusInfo& st = ui.app().status();
    char buf[32];
    std::snprintf(buf, sizeof buf, "%5.1f %2d/%-2d", st.bpm, st.sig_num, st.sig_den);
    widgets::text(c, ui, kRightCol, kRowHeader, buf);
    std::snprintf(buf, sizeof buf, "%03d:%d:%03d", st.bar, st.beat, st.tick);
    widgets::text(c, ui, kRightCol, kRowCommand, buf);
    const char transport = st.playing ? gfx::glyph::kPlay : gfx::glyph::kSquare;
    widgets::text(c, ui, kRightCol + 10, kRowCommand, std::string_view(&transport, 1),
                  st.playing ? gfx::kInkBright : gfx::kInkDim);
}

void draw_command_line(gfx::Canvas& c, const Ui& ui) {
    widgets::text(c, ui, 0, kRowCommand, "COMMAND:");
    const Rect field = cells(kBarCol, kRowCommand, kBarCols, 1);
    c.fill_rect(field, gfx::kInkField);
    ui.add_zone(field, ZoneOwner::Chrome, kZoneCommand);
    const CommandLine& cl = ui.command_line();
    widgets::text(c, ui, kBarCol + 1, kRowCommand, cl.text());
    if (ui.focus() == Focus::Command) {
        // Block cursor, blinking at ~2 Hz (steady in headless renders: time is fixed at 0).
        const bool on = std::fmod(ui.app().time_s(), 1.0) < 0.6;
        if (on) {
            const int col = kBarCol + 1 + static_cast<int>(cl.cursor());
            const Rect cur = cells(col, kRowCommand, 1, 1);
            c.fill_rect(Rect{cur.x, cur.y + 1, cur.w - 1, cur.h - 2}, gfx::kInkBright);
            if (cl.cursor() < cl.text().size()) {
                gfx::draw_text(c, ui.fonts().large, cur.x, cur.y, std::string_view(&cl.text()[cl.cursor()], 1),
                               gfx::kInkBg);
            }
        }
    }
}

void draw_soft_keys(gfx::Canvas& c, const Ui& ui) {
    for (int i = 0; i < kSoftKeyCount; ++i) {
        const Rect r = soft_key_rect(i);
        const std::string command = soft_key_command(ui, i);
        const bool hot = !command.empty() && ui.hovered(ZoneOwner::Chrome, kZoneSoftKey + i);
        c.rect(r, gfx::kInkText);
        // Key name in the top-left corner (the reference uses a narrow side strip, which leaves
        // too little room for a caption at 512 px), caption centered underneath.
        const std::string key_name = i < 12 ? "F" + std::to_string(i + 1) : (i == 12 ? "P" : "S");
        const Rect body{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
        if (hot) {
            c.fill_rect(body, gfx::kInkBright);
        }
        widgets::small_text(c, ui, body.x + 1, body.y + 1, key_name, hot ? gfx::kInkBg : gfx::kInkDim, 5);
        if (command.empty()) {
            c.stipple_rect(Rect{body.x + 2, body.y + 10, body.w - 4, body.h - 12}, gfx::kInkShade);
        } else {
            const std::string caption = i == 12 ? "BACK" : caption_for(command);
            const std::uint8_t ink =
                hot ? gfx::kInkBg : (command_available(command) || i == 12 ? gfx::kInkText : gfx::kInkDim);
            const int text_w = static_cast<int>(caption.size()) * 6 - 1;
            widgets::small_text(c, ui, body.x + (body.w - text_w) / 2, body.y + 12, caption, ink, 6);
        }
        ui.add_zone(r, ZoneOwner::Chrome, kZoneSoftKey + i);
    }
}

void draw_info_line(gfx::Canvas& c, const Ui& ui) {
    c.fill_rect(cells(0, kRowInfo, kCols, 1), gfx::kInkField);
    widgets::text(c, ui, 1, kRowInfo, "INFO:");
    const std::string msg = fit(to_upper(ui.message_text()), 33);
    widgets::text(c, ui, 7, kRowInfo, msg, ui.message_is_error() ? gfx::kInkWarn : gfx::kInkText);

    const StatusInfo& st = ui.app().status();
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s CPU%3d%% V%02d", fit(to_upper(st.project), 8).c_str(),
                  std::clamp(static_cast<int>(std::lround(st.cpu * 100.0f)), 0, 999), std::clamp(st.voices, 0, 99));
    widgets::text(c, ui, 41, kRowInfo, buf, gfx::kInkText);
    const char led = st.midi_activity ? gfx::glyph::kDot : gfx::glyph::kRing;
    widgets::text(c, ui, 62, kRowInfo, std::string_view(&led, 1), st.midi_activity ? gfx::kInkBright : gfx::kInkDim);
}

} // namespace

std::string soft_key_command(const Ui& ui, int index) {
    if (index >= 0 && index < 12) {
        const Key key = static_cast<Key>(static_cast<int>(Key::F1) + index);
        const std::string* cmd = ui.keys().command_for(key);
        return cmd ? *cmd : std::string{};
    }
    if (index == 12) {
        return "BACK";
    }
    if (index == 13) {
        return "PS";
    }
    return {};
}

void draw(gfx::Canvas& canvas, const Ui& ui) {
    draw_header(canvas, ui);
    draw_command_line(canvas, ui);
    draw_soft_keys(canvas, ui);
    draw_info_line(canvas, ui);
}

} // namespace phos::ui::chrome
