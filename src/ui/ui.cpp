#include "ui/ui.h"

#include "core/text.h"
#include "ui/chrome.h"
#include "ui/layout.h"
#include "ui/page_catalog.h"
#include "ui/pages/pages.h"

namespace phos::ui {

Ui::Ui(AppContext& app, const Fonts& fonts, KeyMap keys) : app_(app), fonts_(fonts), keys_(std::move(keys)) {
    pages_.push_back(make_index_page());
    pages_.push_back(make_setup_page());
    pages_.push_back(make_help_page());
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
    if (!pen_inside_) {
        return;
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

void Ui::handle_event(const InputEvent& ev) {
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
        message(*reply, error);
        return !error;
    }
    message("UNKNOWN COMMAND: " + cmd->verb, true);
    return false;
}

std::optional<std::string> Ui::run_ui_command(const Command& cmd) {
    const std::string page_id = page_id_from_verb(cmd.verb);
    if (!page_id.empty()) {
        if (go_to_page(page_id)) {
            return std::string{};
        }
        return std::string{}; // go_to_page already set an error message
    }
    if (cmd.verb == "HELP") {
        go_to_page("H");
        return std::string{};
    }
    if (cmd.verb == "BACK") {
        go_back();
        return std::string{};
    }
    if (cmd.verb == "TESTCARD") {
        go_to_page("TESTCARD");
        return std::string{};
    }
    if (cmd.verb == "THEME" || cmd.verb == "SCALE" || cmd.verb == "CRT") {
        DisplayProfile prof = app_.profile();
        if (cmd.args.empty()) {
            message(cmd.verb + ": " + std::string(find_verb(cmd.verb)->usage), true);
            return std::string{};
        }
        if (cmd.verb == "THEME") {
            const gfx::Theme* t = gfx::find_theme(cmd.args[0]);
            if (!t) {
                message("THEME: UNKNOWN " + to_upper(cmd.args[0]), true);
                return std::string{};
            }
            prof.theme = std::string(t->name);
        } else if (cmd.verb == "SCALE") {
            const auto mode = gfx::parse_scale_mode(cmd.args[0]);
            if (!mode) {
                message("SCALE: FIT, INTEGER OR STRETCH", true);
                return std::string{};
            }
            prof.scale = *mode;
        } else {
            if (!cmd.arg_is(0, "ON") && !cmd.arg_is(0, "OFF")) {
                message("CRT: ON OR OFF", true);
                return std::string{};
            }
            prof.crt.enabled = cmd.arg_is(0, "ON");
        }
        app_.set_profile(prof);
        return cmd.verb + " " + to_upper(cmd.args[0]);
    }
    return std::nullopt;
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

void Ui::draw_pen(gfx::Canvas& canvas) const {
    if (!pen_inside_) {
        return;
    }
    const int x = static_cast<int>(pen_x_);
    const int y = static_cast<int>(pen_y_);
    // Light-pen crosshair with an open centre so the targeted pixel stays visible.
    canvas.hline(x - 7, x - 3, y, gfx::kInkPen);
    canvas.hline(x + 3, x + 7, y, gfx::kInkPen);
    canvas.vline(x, y - 7, y - 3, gfx::kInkPen);
    canvas.vline(x, y + 3, y + 7, gfx::kInkPen);
    canvas.set(x, y, gfx::kInkPen);
}

} // namespace phos::ui
