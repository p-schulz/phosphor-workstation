#include "core/notes.h"

#include <array>
#include <vector>

#include "core/text.h"

namespace phos {

std::optional<int> parse_note_name(std::string_view text) {
    const std::string t = to_upper(trim(text));
    if (t.empty()) {
        return std::nullopt;
    }
    if (t[0] >= '0' && t[0] <= '9') {
        int n = 0;
        for (char c : t) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            n = n * 10 + (c - '0');
            if (n > 127) {
                return std::nullopt;
            }
        }
        return n;
    }
    constexpr std::array<int, 7> kSemitone = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    if (t[0] < 'A' || t[0] > 'G') {
        return std::nullopt;
    }
    int n = kSemitone[static_cast<std::size_t>(t[0] - 'A')];
    std::size_t i = 1;
    if (i < t.size() && (t[i] == '#' || t[i] == 'B')) {
        n += t[i] == '#' ? 1 : -1;
        ++i;
    }
    bool negative = false;
    if (i < t.size() && t[i] == '-') {
        negative = true;
        ++i;
    }
    if (i + 1 != t.size() || t[i] < '0' || t[i] > '9') {
        return std::nullopt;
    }
    const int octave = (t[i] - '0') * (negative ? -1 : 1);
    const int note = (octave + 1) * 12 + n; // C4 = 60
    if (note < 0 || note > 127) {
        return std::nullopt;
    }
    return note;
}

std::string note_name(int note) {
    static constexpr std::array<const char*, 12> kNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127) {
        return "--";
    }
    return std::string(kNames[static_cast<std::size_t>(note % 12)]) + std::to_string(note / 12 - 1);
}

std::optional<int> note_from_filename(std::string_view stem) {
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= stem.size(); ++i) {
        const bool sep = i == stem.size() || stem[i] == '_' || stem[i] == ' ' || stem[i] == '.' || stem[i] == '(' ||
                         stem[i] == ')' || stem[i] == '[' || stem[i] == ']' ||
                         // '-' separates, except in "C-1" style octaves (letter or accidental before it)
                         (stem[i] == '-' && !(i > 0 && i + 1 < stem.size() && stem[i + 1] >= '0' && stem[i + 1] <= '9' &&
                                              i - start <= 2 && i > start));
        if (sep) {
            if (i > start) {
                tokens.push_back(stem.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        const std::string_view t = *it;
        const bool digits = !t.empty() && t.find_first_not_of("0123456789") == std::string_view::npos;
        if (digits) {
            if (t.size() <= 3) {
                const int n = std::stoi(std::string(t));
                if (n >= 21 && n <= 108) {
                    return n;
                }
            }
            continue;
        }
        if (t.size() >= 2 && t.size() <= 4) {
            if (const auto n = parse_note_name(t)) {
                return n;
            }
        }
    }
    return std::nullopt;
}

} // namespace phos
