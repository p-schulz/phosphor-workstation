#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/text.h"
#include "ui/chrome.h"
#include "ui/keyjazz.h"
#include "ui/layout.h"
#include "ui/page_catalog.h"
#include "ui/pages/pages.h"

namespace phos::ui {

Ui::Ui(AppContext& app, const Fonts& fonts, KeyMap keys) : app_(app), fonts_(fonts), keys_(std::move(keys)) {
    pages_.push_back(make_index_page());
    pages_.push_back(make_setup_page());
    pages_.push_back(make_help_page());
    pages_.push_back(make_disk_page());
    pages_.push_back(make_keyboard_page());
    pages_.push_back(make_control_page());
    pages_.push_back(make_sample_page());
    pages_.push_back(make_composer_page());
    pages_.push_back(make_mixer_page());
    pages_.push_back(make_loops_page());
    pages_.push_back(make_harmonics_page());
    pages_.push_back(make_synth_page());
    pages_.push_back(make_wavedraw_page());
    pages_.push_back(make_waveform_page());
    pages_.push_back(make_spectral_page());
    pages_.push_back(make_chip_page());
    pages_.push_back(make_vkeys_page());
    pages_.push_back(make_testcard_page());
    message("READY. TYPE HELP OR CLICK A PAGE");
}

Ui::~Ui() = default;

const gfx::Theme& Ui::theme() const {
    const gfx::Theme* t = gfx::find_theme(app_.profile().theme);
    return t ? *t : gfx::default_theme();
}

void Ui::set_focus(Focus focus) {
    const Page* p = current_page();
    if (focus == Focus::Fields && (!p || !p->has_fields())) {
        focus = Focus::Command;
    }
    focus_ = focus;
}

Page* Ui::find_page(std::string_view id) const {
    for (const auto& p : pages_) {
        if (iequals(p->id(), id)) {
            return p.get();
        }
    }
    return nullptr;
}

Page* Ui::page() { return find_page(current_); }

const Page* Ui::current_page() const { return find_page(current_); }

bool Ui::show_view(std::string_view view) {
    Page* p = page();
    return p && p->show(to_upper(view));
}

std::string_view Ui::current_page_id() const { return current_; }

bool Ui::go_to_page(std::string_view id) {
    const std::string wanted = to_upper(id);
    Page* p = find_page(wanted);
    if (!p) {
        const PageInfo* info = find_page_info(wanted);
        if (info) {
            message("PAGE " + wanted + " ARRIVES IN " + std::string(info->milestone), true);
        } else {
            message("NO PAGE " + wanted, true);
        }
        return false;
    }
    if (wanted != current_) {
        // Full-canvas pages are overlays: BACK from them returns to where we were.
        if (!find_page(current_) || !find_page(current_)->full_canvas()) {
            previous_ = current_;
        }
        current_ = wanted;
    }
    focus_ = Focus::Command;
    p->enter(*this);
    return true;
}

void Ui::go_back() { go_to_page(previous_.empty() ? std::string_view("1") : std::string_view(previous_)); }

void Ui::message(std::string text, bool error) {
    if (text != message_ || error != message_error_) {
        message_time_ = app_.time_s(); // a repeated message keeps rolling where it is
    }
    message_ = std::move(text);
    message_error_ = error;
}

void Ui::add_zone(gfx::Rect rect, ZoneOwner owner, int id) const { zones_.push_back(Zone{rect, owner, id}); }

bool Ui::hovered(ZoneOwner owner, int id) const {
    if (hover_index_ < 0 || hover_index_ >= static_cast<int>(hit_zones_.size())) {
        return false;
    }
    const Zone& z = hit_zones_[static_cast<std::size_t>(hover_index_)];
    return z.owner == owner && z.id == id;
}

void Ui::update(const InputState& input) {
    handle_pen(input);
    for (const InputEvent& ev : input.events) {
        handle_event(ev);
    }
    if (Page* p = page()) {
        p->update(input, *this);
    }
}

void Ui::handle_pen(const InputState& input) {
    pen_inside_ = input.pen_inside;
    pen_x_ = input.pen_x;
    pen_y_ = input.pen_y;
    hover_index_ = -1;
    if (side_.handle_pen(input, *this) || !pen_inside_) {
        return; // over a side strip, or outside
    }
    if (const Page* p = current_page(); p && p->full_canvas()) {
        if (input.pen_pressed) {
            go_back(); // overlays close on a pen click as well as on any key
        }
        return;
    }
    const int x = static_cast<int>(pen_x_);
    const int y = static_cast<int>(pen_y_);
    // Last registered zone wins (drawn on top).
    for (int i = static_cast<int>(hit_zones_.size()) - 1; i >= 0; --i) {
        if (hit_zones_[static_cast<std::size_t>(i)].rect.contains(x, y)) {
            hover_index_ = i;
            break;
        }
    }
    if (hover_index_ < 0) {
        return;
    }
    const Zone zone = hit_zones_[static_cast<std::size_t>(hover_index_)];
    if (input.pen_pressed) {
        if (zone.owner == ZoneOwner::Chrome) {
            handle_chrome_zone(zone.id);
        } else if (Page* p = page()) {
            if (p->has_fields()) {
                focus_ = Focus::Fields;
            }
            p->on_zone(zone.id, 0, *this);
        }
    } else if (input.wheel != 0 && zone.owner == ZoneOwner::Page) {
        if (Page* p = page()) {
            p->on_zone(zone.id, input.wheel, *this);
        }
    }
}

void Ui::side_key(SideKey key) {
    auto press = [&](Key k) {
        InputEvent ev;
        ev.key = k;
        handle_event(ev);
    };
    switch (key) {
    case SideKey::Index:
        execute("P1");
        break;
    case SideKey::PagePrev:
    case SideKey::PageNext:
        step_page(key == SideKey::PageNext ? 1 : -1);
        break;
    case SideKey::Back:
        go_back();
        break;
    case SideKey::Escape:
        press(Key::Escape);
        break;
    case SideKey::Up:
        if (focus_ == Focus::Fields) { // on the command line UP would recall history
            press(Key::Up);
        }
        break;
    case SideKey::Down:
        press(Key::Down);
        break;
    case SideKey::Plus:
    case SideKey::Minus:
        value_step(key == SideKey::Plus ? 1 : -1);
        break;
    case SideKey::Enter:
        press(Key::Enter);
        break;
    }
}

// + / -: the page's own +/- if it has them, else RIGHT / LEFT (pages that change values with the
// arrows). Never types into the command line.
void Ui::value_step(int dir) {
    Page* p = page();
    if (!p || p->full_canvas()) {
        return;
    }
    if (p->has_fields()) {
        focus_ = Focus::Fields;
    }
    InputEvent text;
    text.type = InputEvent::Type::Text;
    text.ch = dir > 0 ? '+' : '-';
    if (p->on_key(text, *this)) {
        return;
    }
    InputEvent arrow;
    arrow.key = dir > 0 ? Key::Right : Key::Left;
    p->on_key(arrow, *this);
}

// PAGE < / >: the next implemented page in catalog order (1, 2, 3 ... S, H).
void Ui::step_page(int dir) {
    std::vector<std::string_view> ids;
    for (const PageInfo& info : kPageCatalog) {
        if (find_page(info.id)) {
            ids.push_back(info.id);
        }
    }
    if (ids.empty()) {
        return;
    }
    const auto n = static_cast<int>(ids.size());
    const auto it = std::find(ids.begin(), ids.end(), std::string_view(current_));
    const int at = it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
    execute("P" + std::string(ids[static_cast<std::size_t>((at + dir + n) % n)]));
}

void Ui::handle_chrome_zone(int id) {
    if (id == chrome::kZoneIndex) {
        go_to_page("1");
    } else if (id == chrome::kZoneCommand) {
        focus_ = Focus::Command;
    } else if (id >= chrome::kZoneSoftKey && id < chrome::kZoneSoftKey + chrome::kSoftKeyCount) {
        const std::string cmd = chrome::soft_key_command(*this, id - chrome::kZoneSoftKey);
        if (!cmd.empty()) {
            execute(cmd);
        }
    }
}

void Ui::set_keyjazz(bool on) {
    if (keyjazz_ == on) {
        return;
    }
    keyjazz_ = on;
    if (!on) {
        release_keyjazz_notes();
    } else {
        command_.clear();
        focus_ = Focus::Command;
    }
}

void Ui::keyjazz_octave(int delta) {
    MidiConfig cfg = app_.midi_config();
    cfg.keyjazz_octave = std::clamp(cfg.keyjazz_octave + delta, 0, 8);
    app_.set_midi_config(cfg);
    message("KEYJAZZ OCTAVE " + std::to_string(cfg.keyjazz_octave));
}

void Ui::release_keyjazz_notes() {
    for (auto& held : keyjazz_down_) {
        if (held > 0) {
            app_.keyjazz_note(held - 1, false);
            held = 0;
        }
    }
}

bool Ui::handle_keyjazz(const InputEvent& ev) {
    if (ev.type == InputEvent::Type::Text) {
        return true; // no typing while KEYJAZZ is on
    }
    if (ev.key == Key::Escape && !ev.release && ev.mods == 0) { // MOD+ESC stays PANIC
        set_keyjazz(false);
        message("KEYJAZZ OFF");
        suppress_text_ = true;
        return true;
    }
    if (ev.key != Key::Char || (ev.mods & ~kModShift) != 0) {
        return false; // F-keys, arrows, MOD chords keep working
    }
    const auto idx = static_cast<std::size_t>(static_cast<unsigned char>(ev.ch) & 0x7F);
    if (ev.release) {
        if (keyjazz_down_[idx] > 0) {
            app_.keyjazz_note(keyjazz_down_[idx] - 1, false);
            keyjazz_down_[idx] = 0;
        }
        return true;
    }
    if (ev.repeat) {
        return true;
    }
    if (ev.ch == keyjazz::kOctaveDown || ev.ch == keyjazz::kOctaveUp) {
        keyjazz_octave(ev.ch == keyjazz::kOctaveUp ? 1 : -1);
        return true;
    }
    const int n = keyjazz::note(ev.ch, app_.midi_config().keyjazz_octave);
    if (n >= 0 && keyjazz_down_[idx] == 0) {
        keyjazz_down_[idx] = n + 1; // remember: the octave may change before the key goes up
        app_.keyjazz_note(n, true);
    }
    return true;
}

void Ui::handle_event(const InputEvent& ev) {
    // Page key: <modifier> + page id jumps there (P = back). It wins over key-map chords with the
    // same keys (Page S lists the ones it hides), so the chosen modifier always works.
    if (ev.type == InputEvent::Type::Key && ev.key == Key::Char && !ev.release && !ev.repeat &&
        app_.page_modifier() != 0 && (ev.mods & ~kModShift) == app_.page_modifier()) {
        const std::string id(1, ev.ch);
        if (ev.ch == 'P' || find_page_info(id)) {
            if (ev.ch == 'P') {
                go_back();
            } else {
                execute("P" + id);
            }
            suppress_text_ = true; // Option+letter on macOS also sends a character
            return;
        }
    }
    if (keyjazz_ && handle_keyjazz(ev)) {
        return;
    }
    if (ev.type == InputEvent::Type::Key && ev.release) {
        return; // only KEYJAZZ uses key releases
    }
    Page* p = page();
    if (ev.type == InputEvent::Type::Text && suppress_text_) {
        suppress_text_ = false;
        return;
    }
    if (ev.type == InputEvent::Type::Key) {
        suppress_text_ = false;
    }

    // Full-canvas overlays (TESTCARD) close on any key.
    if (p && p->full_canvas()) {
        if (ev.type == InputEvent::Type::Key) {
            go_back();
            suppress_text_ = true;
        }
        return;
    }

    if (ev.type == InputEvent::Type::Key) {
        // Global bindings. Printable keys (SPACE, letters) only fire while the command line is
        // empty, so typing a command never triggers them.
        const bool printable = ev.key == Key::Space || (ev.key == Key::Char && (ev.mods & ~kModShift) == 0);
        if (!printable || command_.empty()) {
            if (const std::string* cmd = keys_.lookup(ev)) {
                execute(*cmd);
                suppress_text_ = printable;
                return;
            }
        }
    }

    if (focus_ == Focus::Fields && p) {
        if (ev.type == InputEvent::Type::Key && ev.key == Key::Escape) {
            focus_ = Focus::Command;
            return;
        }
        if (p->on_key(ev, *this)) {
            // A page that used a letter key: drop the character the OS sends after it.
            suppress_text_ = ev.type == InputEvent::Type::Key && (ev.key == Key::Char || ev.key == Key::Space);
            return;
        }
        if (ev.type == InputEvent::Type::Text) {
            // Typing anything the page does not use goes to the command line.
            focus_ = Focus::Command;
            command_.insert(ev.ch);
        }
        return;
    }

    if (ev.type == InputEvent::Type::Text) {
        command_.insert(ev.ch);
        return;
    }
    handle_command_key(ev);
}

void Ui::handle_command_key(const InputEvent& ev) {
    switch (ev.key) {
    case Key::Enter: {
        const std::string text = command_.submit();
        if (!text.empty()) {
            execute(text);
        }
        break;
    }
    case Key::Backspace:
        command_.backspace();
        break;
    case Key::Delete:
        command_.erase();
        break;
    case Key::Left:
        command_.left();
        break;
    case Key::Right:
        command_.right();
        break;
    case Key::Home:
        command_.home();
        break;
    case Key::End:
        command_.end();
        break;
    case Key::Up:
        command_.history_prev();
        break;
    case Key::Down:
        if (command_.browsing_history()) {
            command_.history_next();
        } else if (command_.empty()) {
            set_focus(Focus::Fields);
        }
        break;
    case Key::Tab:
        command_.complete();
        break;
    case Key::Escape:
        command_.clear();
        break;
    default:
        break;
    }
}

bool Ui::execute(std::string_view text) {
    const auto cmd = parse_command(text);
    if (!cmd) {
        return true;
    }
    if (auto reply = run_ui_command(*cmd)) {
        if (!reply->empty()) {
            message(*reply);
        }
        return !message_error_;
    }
    if (const VerbInfo* v = find_verb(cmd->verb); v && !v->milestone.empty()) {
        message(cmd->verb + ": NOT IN THIS BUILD (" + std::string(v->milestone) + ")", true);
        return false;
    }
    if (auto reply = app_.run_host_command(*cmd)) {
        const bool error = reply->rfind("ERROR", 0) == 0;
        if (!reply->empty()) { // an empty reply (WAIT, SNAP, SHOT) keeps the current message
            message(*reply, error);
        }
        return !error;
    }
    message("UNKNOWN COMMAND: " + cmd->verb, true);
    return false;
}

void Ui::draw(gfx::Canvas& canvas) {
    zones_.clear();
    canvas.reset_clip();
    canvas.clear(gfx::kInkBg);
    const Page* p = current_page();
    if (p && p->full_canvas()) {
        p->draw(canvas, *this);
    } else {
        chrome::draw(canvas, *this);
        if (p) {
            canvas.set_clip(cells(0, kBodyTop, kCols, kBodyRows));
            p->draw(canvas, *this);
            canvas.reset_clip();
        }
    }
    hit_zones_ = zones_;
}

void Ui::draw_pen(gfx::Canvas& canvas, int origin_x) const {
    if (!pen_inside_) {
        return;
    }
    const int x = static_cast<int>(std::floor(pen_x_)) + origin_x;
    const int y = static_cast<int>(pen_y_);
    // Light-pen crosshair with an open centre so the targeted pixel stays visible.
    canvas.hline(x - 7, x - 3, y, gfx::kInkPen);
    canvas.hline(x + 3, x + 7, y, gfx::kInkPen);
    canvas.vline(x, y - 7, y - 3, gfx::kInkPen);
    canvas.vline(x, y + 3, y + 7, gfx::kInkPen);
    canvas.set(x, y, gfx::kInkPen);
}

} // namespace phos::ui
