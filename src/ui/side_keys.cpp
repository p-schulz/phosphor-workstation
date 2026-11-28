#include "ui/side_keys.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "ui/layout.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

constexpr int kMargin = 4; // around the keys, canvas pixels
constexpr int kGap = 6;    // between keys

bool repeats(SideKey k) {
    return k == SideKey::Up || k == SideKey::Down || k == SideKey::Plus || k == SideKey::Minus;
}

// Filled triangle pointing up / down / left / right, centred on (cx, cy), `s` = half size.
void triangle(gfx::Canvas& c, int cx, int cy, int s, char dir, std::uint8_t ink) {
    for (int i = 0; i <= s; ++i) {
        switch (dir) {
        case 'U':
            c.hline(cx - i, cx + i, cy - s / 2 + i, ink);
            break;
        case 'D':
            c.hline(cx - i, cx + i, cy + s / 2 - i, ink);
            break;
        case 'L':
            c.vline(cx - s / 2 + i, cy - i, cy + i, ink);
            break;
        default:
            c.vline(cx + s / 2 - i, cy - i, cy + i, ink);
            break;
        }
    }
}

// The label in the 8x12 font if it fits, else in the 8x8 one.
void label(gfx::Canvas& c, const Ui& ui, int cx, int cy, std::string_view s, int room, std::uint8_t ink) {
    const int large_w = static_cast<int>(s.size()) * kCellW;
    if (large_w + 4 <= room) {
        widgets::text_px(c, ui, cx - large_w / 2, cy - kCellH / 2, s, ink);
    } else {
        const int small_w = static_cast<int>(s.size()) * 6;
        widgets::small_text(c, ui, cx - small_w / 2, cy - 4, s, ink);
    }
}

} // namespace

void SideKeys::set_width(int w) {
    width_ = std::max(0, w);
    if (width_ == 0) {
        held_ = hover_ = -1;
    }
}

gfx::Rect SideKeys::key_rect(SideKey key) const {
    const int i = static_cast<int>(key);
    const int slot = i % kSideKeysPerEdge;
    const int h = (kCanvasH - 2 * kMargin - (kSideKeysPerEdge - 1) * kGap) / kSideKeysPerEdge;
    const int x = i < kSideKeysPerEdge ? -width_ + kMargin : kCanvasW + kMargin;
    return gfx::Rect{x, kMargin + slot * (h + kGap), width_ - 2 * kMargin, h};
}

int SideKeys::key_at(float x, float y) const {
    if (width_ <= 0) {
        return -1;
    }
    const auto px = static_cast<int>(std::floor(x));
    const auto py = static_cast<int>(std::floor(y));
    for (int k = 0; k < kSideKeyCount; ++k) {
        if (key_rect(static_cast<SideKey>(k)).contains(px, py)) {
            return k;
        }
    }
    return -1;
}

bool SideKeys::handle_pen(const InputState& in, Ui& ui) {
    const bool over = width_ > 0 && in.pen_inside && (in.pen_x < 0.0f || in.pen_x >= static_cast<float>(kCanvasW));
    hover_ = over ? key_at(in.pen_x, in.pen_y) : -1;
    if (in.pen_pressed && hover_ >= 0) {
        held_ = hover_;
        next_repeat_s_ = in.time_s + kRepeatDelay_s;
        ui.side_key(static_cast<SideKey>(held_));
    } else if (held_ >= 0) {
        if (!in.pen_down || hover_ != held_) {
            held_ = -1; // released, or slid off the key
        } else if (repeats(static_cast<SideKey>(held_)) && in.time_s >= next_repeat_s_) {
            next_repeat_s_ = in.time_s + kRepeatInterval_s;
            ui.side_key(static_cast<SideKey>(held_));
        }
    }
    return over;
}

void SideKeys::draw(gfx::Canvas& screen, const Ui& ui) const {
    if (width_ <= 0) {
        return;
    }
    for (int k = 0; k < kSideKeyCount; ++k) {
        const auto key = static_cast<SideKey>(k);
        gfx::Rect r = key_rect(key);
        r.x += width_; // canvas -> screen coordinates
        const bool pressed = k == held_;
        if (pressed) {
            screen.fill_rect(r, gfx::kInkSelect);
        }
        screen.rect(r, pressed ? gfx::kInkSelect : k == hover_ ? gfx::kInkBright : gfx::kInkText);
        const std::uint8_t ink = pressed ? gfx::kInkBg : gfx::kInkText;
        const int cx = r.x + r.w / 2;
        const int cy = r.y + r.h / 2;
        const int s = std::clamp(std::min(r.w, r.h) / 4, 4, 14); // icon half size
        switch (key) {
        case SideKey::Index:
            label(screen, ui, cx, cy, "INDEX", r.w, ink);
            break;
        case SideKey::PagePrev:
        case SideKey::PageNext:
            triangle(screen, cx, cy - 8, s / 2 + 2, key == SideKey::PagePrev ? 'L' : 'R', ink);
            label(screen, ui, cx, cy + 10, "PAGE", r.w, ink);
            break;
        case SideKey::Back:
            label(screen, ui, cx, cy, "BACK", r.w, ink);
            break;
        case SideKey::Escape:
            label(screen, ui, cx, cy, "ESC", r.w, ink);
            break;
        case SideKey::Up:
        case SideKey::Down:
            triangle(screen, cx, cy, s, key == SideKey::Up ? 'U' : 'D', ink);
            break;
        case SideKey::Plus:
            screen.fill_rect(gfx::Rect{cx - s, cy - 1, 2 * s + 1, 3}, ink);
            screen.fill_rect(gfx::Rect{cx - 1, cy - s, 3, 2 * s + 1}, ink);
            break;
        case SideKey::Minus:
            screen.fill_rect(gfx::Rect{cx - s, cy - 1, 2 * s + 1, 3}, ink);
            break;
        case SideKey::Enter:
            label(screen, ui, cx, cy, "ENTER", r.w, ink);
            break;
        }
    }
}

} // namespace phos::ui
