#include "ui/key_map.h"

#include <array>
#include <utility>

#include "core/text.h"

namespace phos::ui {

namespace {

constexpr std::array<std::pair<std::string_view, Key>, 29> kNamedKeys = {{
    {"ESC", Key::Escape},      {"ESCAPE", Key::Escape}, {"ENTER", Key::Enter},   {"RETURN", Key::Enter},
    {"TAB", Key::Tab},         {"BACKSPACE", Key::Backspace}, {"DELETE", Key::Delete}, {"INSERT", Key::Insert},
    {"LEFT", Key::Left},       {"RIGHT", Key::Right},   {"UP", Key::Up},         {"DOWN", Key::Down},
    {"HOME", Key::Home},       {"END", Key::End},       {"PAGEUP", Key::PageUp}, {"PAGEDOWN", Key::PageDown},
    {"SPACE", Key::Space},     {"F1", Key::F1},         {"F2", Key::F2},         {"F3", Key::F3},
    {"F4", Key::F4},           {"F5", Key::F5},         {"F6", Key::F6},         {"F7", Key::F7},
    {"F8", Key::F8},           {"F9", Key::F9},         {"F10", Key::F10},       {"F11", Key::F11},
    {"F12", Key::F12},
}};

constexpr std::string_view kDefaultKeys = R"(# PHOSPHOR key bindings. Copy to keys.cfg in the settings folder to customise.
# Syntax: <chord> = <command>. Modifiers: SHIFT, CTRL, ALT, CMD, MOD (Cmd on macOS, Ctrl elsewhere).
# Works with remotes that emulate a keyboard (e.g. Flirc): bind whatever keys they send.
# Page jumps (modifier + page id, P = back) are set on Page S (PAGE KEY) or with PAGEKEY.
F1 = P1
F2 = P2
F3 = P3
F4 = P4
F5 = P5
F6 = P6
F7 = P7
F8 = P8
F9 = PC
F10 = PL
F11 = PM
F12 = PR
SPACE = PLAY
MOD+S = SAVE
MOD+Z = UNDO
MOD+SHIFT+Z = REDO
MOD+SHIFT+D = DISPLAY NEXT
MOD+K = KEYJAZZ
MOD+ESC = PANIC
ALT+ENTER = DISPLAY TOGGLE
MOD+Q = QUIT
)";

} // namespace

std::optional<KeyChord> parse_chord(std::string_view text, bool mac) {
    KeyChord chord;
    std::string rest = to_upper(trim(text));
    for (;;) {
        const auto plus = rest.find('+');
        if (plus == std::string::npos || plus + 1 >= rest.size()) {
            break; // last part is the key itself ("+" alone stays a key)
        }
        const std::string mod = rest.substr(0, plus);
        rest = rest.substr(plus + 1);
        if (mod == "SHIFT") {
            chord.mods |= kModShift;
        } else if (mod == "CTRL" || mod == "CONTROL") {
            chord.mods |= kModCtrl;
        } else if (mod == "ALT" || mod == "OPTION") {
            chord.mods |= kModAlt;
        } else if (mod == "CMD" || mod == "SUPER") {
            chord.mods |= kModSuper;
        } else if (mod == "MOD") {
            chord.mods |= mac ? kModSuper : kModCtrl;
        } else {
            return std::nullopt;
        }
    }
    for (const auto& [name, key] : kNamedKeys) {
        if (rest == name) {
            chord.key = key;
            return chord;
        }
    }
    if (rest.size() == 1 && static_cast<unsigned char>(rest[0]) > 0x20 && static_cast<unsigned char>(rest[0]) < 0x7F) {
        chord.key = Key::Char;
        chord.ch = rest[0];
        return chord;
    }
    return std::nullopt;
}

std::string chord_name(const KeyChord& chord) {
    std::string out;
    if (chord.mods & kModCtrl) {
        out += "CTRL+";
    }
    if (chord.mods & kModAlt) {
        out += "ALT+";
    }
    if (chord.mods & kModSuper) {
        out += "CMD+";
    }
    if (chord.mods & kModShift) {
        out += "SHIFT+";
    }
    if (chord.key == Key::Char) {
        return out + chord.ch;
    }
    for (const auto& [name, key] : kNamedKeys) {
        if (key == chord.key) {
            return out + std::string(name);
        }
    }
    return out + "?";
}

KeyMap KeyMap::defaults(bool mac) {
    auto map = parse(kDefaultKeys, mac, nullptr);
    return map ? *map : KeyMap{};
}

std::optional<KeyMap> KeyMap::parse(std::string_view text, bool mac, std::string* error) {
    KeyMap map;
    int line_no = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view raw = text.substr(pos, nl == std::string_view::npos ? text.size() - pos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        ++line_no;
        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        // The chord itself may be "=", so split at the last " = " / "=" after the first character.
        const std::size_t eq = line.find('=', 1);
        if (eq == std::string_view::npos) {
            if (error) {
                *error = "keys line " + std::to_string(line_no) + ": expected <chord> = <command>";
            }
            return std::nullopt;
        }
        const auto chord = parse_chord(line.substr(0, eq), mac);
        const std::string_view command = trim(line.substr(eq + 1));
        if (!chord || command.empty()) {
            if (error) {
                *error = "keys line " + std::to_string(line_no) + ": bad binding '" + std::string(line) + "'";
            }
            return std::nullopt;
        }
        map.bind(*chord, std::string(command));
    }
    return map;
}

void KeyMap::bind(KeyChord chord, std::string command) {
    for (auto& b : bindings_) {
        if (b.chord == chord) {
            b.command = std::move(command);
            return;
        }
    }
    bindings_.push_back(Binding{chord, std::move(command)});
}

const std::string* KeyMap::lookup(const InputEvent& event) const {
    if (event.type != InputEvent::Type::Key) {
        return nullptr;
    }
    for (const auto& b : bindings_) {
        if (b.chord.key != event.key || b.chord.mods != event.mods) {
            continue;
        }
        if (event.key == Key::Char && b.chord.ch != event.ch) {
            continue;
        }
        return &b.command;
    }
    return nullptr;
}

const std::string* KeyMap::command_for(Key key) const {
    for (const auto& b : bindings_) {
        if (b.chord.key == key && b.chord.mods == 0) {
            return &b.command;
        }
    }
    return nullptr;
}

std::string_view default_keys_text() { return kDefaultKeys; }

} // namespace phos::ui
