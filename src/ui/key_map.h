#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/input.h"

namespace phos::ui {

struct KeyChord {
    Key key = Key::None;
    char ch = 0; // for Key::Char
    int mods = 0;
    friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

// Parses "F1", "SPACE", "CTRL+SHIFT+D", "MOD+S" (MOD = Cmd on macOS, Ctrl elsewhere).
std::optional<KeyChord> parse_chord(std::string_view text, bool mac);
std::string chord_name(const KeyChord& chord);

// Key bindings: chord → command line text. File format (conf/keys.example.cfg):
//   # comment
//   F1 = P1
//   MOD+SHIFT+D = DISPLAY NEXT
class KeyMap {
public:
    static KeyMap defaults(bool mac);
    static std::optional<KeyMap> parse(std::string_view text, bool mac, std::string* error);

    void bind(KeyChord chord, std::string command);
    // Command bound to this key event, or nullptr.
    const std::string* lookup(const InputEvent& event) const;
    // Command bound to an unmodified key (used for the soft-key captions).
    const std::string* command_for(Key key) const;

    std::size_t size() const { return bindings_.size(); }

private:
    struct Binding {
        KeyChord chord;
        std::string command;
    };
    std::vector<Binding> bindings_;
};

// Text of the default key map; conf/keys.example.cfg holds the same bindings.
std::string_view default_keys_text();

} // namespace phos::ui
