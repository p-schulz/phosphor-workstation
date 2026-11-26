#pragma once

#include <array>
#include <string_view>

// KEYJAZZ: the computer keyboard as a two-octave piano (tracker layout). Keys are physical
// positions named after the US layout (GLFW key tokens), so on QWERTZ the bottom-left note key is
// the one labelled Y.
namespace phos::ui::keyjazz {

struct KeyNote {
    char key;   // uppercase US label, as in InputEvent::ch
    int offset; // semitones above C of the base octave
};

// Lower row: white keys Z X C V B N M , . /  black keys S D G H J L ;
// Upper row (one octave up): white Q W E R T Y U I O P  black 2 3 5 6 7 9 0
inline constexpr std::array<KeyNote, 34> kKeys = {{
    {'Z', 0},  {'S', 1},  {'X', 2},  {'D', 3},  {'C', 4},  {'V', 5},  {'G', 6},  {'B', 7},  {'H', 8},
    {'N', 9},  {'J', 10}, {'M', 11}, {',', 12}, {'L', 13}, {'.', 14}, {';', 15}, {'/', 16},
    {'Q', 12}, {'2', 13}, {'W', 14}, {'3', 15}, {'E', 16}, {'R', 17}, {'5', 18}, {'T', 19}, {'6', 20},
    {'Y', 21}, {'7', 22}, {'U', 23}, {'I', 24}, {'9', 25}, {'O', 26}, {'0', 27}, {'P', 28},
}};

inline constexpr char kOctaveDown = '[';
inline constexpr char kOctaveUp = ']';

// Semitone offset for a key, or -1.
constexpr int offset(char key) {
    for (const auto& k : kKeys) {
        if (k.key == key) {
            return k.offset;
        }
    }
    return -1;
}

// MIDI note for a key at `octave` (C<octave>; C4 = 60), or -1 when unmapped or out of range.
constexpr int note(char key, int octave) {
    const int off = offset(key);
    if (off < 0) {
        return -1;
    }
    const int n = (octave + 1) * 12 + off;
    return n >= 0 && n <= 127 ? n : -1;
}

} // namespace phos::ui::keyjazz
