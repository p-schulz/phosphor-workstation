#include "ui/ui.h"

#include <algorithm>
#include <cstdio>

#include "content/gm.h"
#include "core/text.h"
#include "ui/chrome.h"
#include "ui/keyjazz.h"
#include "ui/song_commands.h"
#include "ui/layout.h"
#include "io/instrument_io.h"
#include "io/smf.h"
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

std::optional<std::string> Ui::run_ui_command(const Command& cmd) {
    const std::string page_id = page_id_from_verb(cmd.verb);
    if (!page_id.empty()) {
        if (go_to_page(page_id)) {
            message(std::string(page()->title())); // replaces an older (error) message
            if (!cmd.args.empty() && !page()->show(to_upper(cmd.joined_args()))) {
                message("PAGE " + page_id + " HAS NO VIEW " + to_upper(cmd.joined_args()), true);
            }
            return std::string{};
        }
        return std::string{}; // go_to_page already set an error message
    }
    if (cmd.verb == "FOLLOW") {
        if (!cmd.args.empty() && !cmd.arg_is(0, "ON") && !cmd.arg_is(0, "OFF")) {
            message("FOLLOW [ON|OFF]", true);
            return std::string{};
        }
        const bool on = cmd.args.empty() ? !app_.follow() : cmd.arg_is(0, "ON");
        app_.set_follow(on);
        return std::string(on ? "FOLLOW ON: PAGE R SCROLLS WITH THE PLAYHEAD" : "FOLLOW OFF: PAGE R STAYS WHERE YOU EDIT");
    }
    if (cmd.verb == "PAGEKEY") {
        const auto mods = cmd.args.empty() ? std::nullopt : page_modifier_from(cmd.args[0]);
        if (!mods) {
            message("PAGEKEY OFF|CTRL|ALT|CMD", true);
            return std::string{};
        }
        app_.set_page_modifier(*mods);
        return *mods == 0 ? std::string("PAGE KEY OFF")
                          : std::string("PAGE KEY ") + page_modifier_name(*mods) + ": " + page_modifier_name(*mods) +
                                "+K = PAGE K, +P = BACK";
    }
    if (cmd.verb == "HELP") {
        go_to_page("H");
        return std::string{};
    }
    if (cmd.verb == "BACK") {
        go_back();
        return std::string{};
    }
    if (cmd.verb == "KEYJAZZ") {
        if (cmd.arg_is(0, "OCTAVE") && cmd.args.size() > 1) {
            MidiConfig cfg = app_.midi_config();
            try {
                cfg.keyjazz_octave = std::clamp(std::stoi(cmd.args[1]), 0, 8);
            } catch (...) {
                message("KEYJAZZ OCTAVE 0-8", true);
                return std::string{};
            }
            app_.set_midi_config(cfg);
            return "KEYJAZZ OCTAVE " + std::to_string(cfg.keyjazz_octave);
        }
        const bool on = cmd.arg_is(0, "ON") || (!cmd.arg_is(0, "OFF") && !keyjazz_);
        set_keyjazz(on);
        return std::string(on ? "KEYJAZZ ON: KEYS PLAY NOTES, ESC = OFF" : "KEYJAZZ OFF");
    }
    if (cmd.verb == "PANIC") {
        release_keyjazz_notes(); // then the host silences the engine (and MIDI out, M8)
    }
    if (auto reply = run_song_command(*this, cmd, taps_)) {
        return reply;
    }
    if (auto reply = run_mix_command(*this, cmd)) {
        return reply;
    }
    if (cmd.verb == "UNDO" || cmd.verb == "REDO") {
        const auto label = cmd.verb == "UNDO" ? app_.workspace().undo() : app_.workspace().redo();
        if (!label) {
            message(cmd.verb + ": NOTHING TO " + cmd.verb, true);
            return std::string{};
        }
        return cmd.verb + ": " + *label;
    }
    if (cmd.verb == "INST") {
        try {
            const int n = cmd.args.empty() ? -1 : std::stoi(cmd.args[0]);
            if (n < 1 || n > model::kInstruments) {
                throw std::out_of_range("inst");
            }
            app_.workspace().select(n - 1);
            const auto& inst = app_.workspace().doc().current_instrument();
            return "INSTRUMENT " + std::to_string(n) + ": " + inst.name;
        } catch (...) {
            message("INST 1-16", true);
            return std::string{};
        }
    }
    if (cmd.verb == "IMPORT") {
        if (cmd.args.empty()) {
            message("IMPORT <PATH>", true);
            return std::string{};
        }
        // An instrument bundle (a path, or a name in the library) replaces the current instrument.
        const std::filesystem::path lib = app_.projects_folder() / "Instruments";
        const std::filesystem::path ipath = io::instrument_path(lib, cmd.joined_args());
        if (io::is_instrument_bundle(ipath)) {
            std::string error;
            const std::uint32_t rate = app_.audio().status.sample_rate ? app_.audio().status.sample_rate : 48000;
            auto inst = io::load_instrument(ipath, rate, &error);
            if (!inst) {
                message("IMPORT FAILED: " + to_upper(error), true);
                return std::string{};
            }
            const int slot = app_.workspace().doc().current;
            const std::string name = inst->name;
            app_.workspace().set_instrument(slot, std::move(*inst), "IMPORT INSTRUMENT");
            return "INSTRUMENT " + std::to_string(slot + 1) + " = " + name + " (" +
                   model::kind_name(app_.workspace().doc().current_instrument().kind) + ", UNDO: MOD+Z)";
        }
        if (ipath.extension() == io::kInstrumentExt && std::filesystem::path(cmd.joined_args()).extension() == io::kInstrumentExt) {
            message("NO INSTRUMENT " + to_upper(cmd.joined_args()) + " (LIBRARY LISTS THEM)", true);
            return std::string{};
        }
        if (io::is_midi_file(cmd.joined_args())) {
            // A Standard MIDI file replaces the song; its tracks get instruments in empty slots
            // (library first, then GM presets). One undoable step.
            std::string error;
            const auto imp = io::read_smf(cmd.joined_args(), app_.workspace().doc().song, &error);
            if (!imp) {
                message("IMPORT FAILED: " + to_upper(error), true);
                return std::string{};
            }
            model::Document doc = app_.workspace().doc();
            const std::uint32_t rate = app_.audio().status.sample_rate ? app_.audio().status.sample_rate : 48000;
            const auto a = content::assemble_midi(doc, *imp, lib, rate);
            app_.workspace().set_document(std::move(doc), "IMPORT MIDI");
            for (const auto& l : a.lines) {
                std::fprintf(stdout, "midi import: %s\n", l.c_str());
            }
            return "MIDI: " + std::to_string(imp->notes) + " NOTES, " + std::to_string(imp->tracks) + " TRACKS, " +
                   std::to_string(imp->sections) + " SECTIONS; INSTRUMENTS " + std::to_string(a.from_library) + " LIBRARY " +
                   std::to_string(a.built_in) + " PRESET " + std::to_string(a.kept) + " KEPT (UNDO: MOD+Z)" +
                   (imp->warning.empty() ? "" : ". " + imp->warning);
        }
        app_.import_sample(cmd.joined_args(), app_.workspace().doc().current);
        return app_.import_status().empty() ? std::string("IMPORTED") : app_.import_status();
    }
    if (cmd.verb == "LIBRARY") {
        // The instrument library on Page 2 (created on first use).
        const auto lib = app_.projects_folder() / "Instruments";
        std::error_code ec;
        std::filesystem::create_directories(lib, ec);
        app_.set_browse_folder(lib);
        go_to_page("2");
        return "LIBRARY: " + std::to_string(io::list_instruments(lib).size()) + " INSTRUMENTS. ENTER LOADS ONE";
    }
    if (cmd.verb == "EXPORT" && (cmd.arg_is(0, "INST") || cmd.arg_is(0, "INSTRUMENT"))) {
        // EXPORT INST [name | path.phinst]: the current instrument into the library.
        const model::Instrument& inst = app_.workspace().doc().current_instrument();
        std::string name;
        for (std::size_t i = 1; i < cmd.args.size(); ++i) {
            name += (i > 1 ? " " : "") + cmd.args[i];
        }
        if (name.empty()) {
            name = inst.name;
        }
        const auto path = io::instrument_path(app_.projects_folder() / "Instruments", name);
        model::Instrument named = inst;
        if (cmd.args.size() > 1 && path.extension() == io::kInstrumentExt && !std::filesystem::path(name).has_parent_path()) {
            named.name = to_upper(path.stem().string());
        }
        std::string error;
        if (!io::save_instrument(path, named, &error)) {
            message("EXPORT FAILED: " + to_upper(error), true);
            return std::string{};
        }
        return "EXPORTED " + to_upper(path.filename().string()) + " (LIBRARY: IMPORT " + to_upper(path.stem().string()) + ")";
    }
    if (cmd.verb == "EXPORT") {
        // EXPORT MIDI [path.mid]: the song as a type 1 Standard MIDI file.
        if (!cmd.arg_is(0, "MIDI")) {
            message("EXPORT MIDI [PATH.MID] | EXPORT INST [NAME]", true);
            return std::string{};
        }
        std::string path;
        for (std::size_t i = 1; i < cmd.args.size(); ++i) {
            path += (i > 1 ? " " : "") + cmd.args[i];
        }
        const std::string name = app_.project().name.empty() ? std::string("UNTITLED") : app_.project().name;
        std::filesystem::path out = path.empty() ? app_.projects_folder() / "Exports" / (name + ".mid") : std::filesystem::path(path);
        if (out.extension().empty()) {
            out += ".mid";
        }
        std::string error;
        if (!io::write_smf(out, app_.workspace().doc(), name, &error)) {
            message("EXPORT FAILED: " + to_upper(error), true);
            return std::string{};
        }
        return "EXPORTED " + to_upper(out.filename().string()) + " (960 PPQ, TEMPO + SIGNATURE)";
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
