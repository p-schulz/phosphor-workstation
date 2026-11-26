#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace phos {

// "A4" -> 69, "C#3" -> 49, "Bb2" -> 46, "C-1" -> 0, "60" -> 60 (C4 = 60). nullopt if invalid.
std::optional<int> parse_note_name(std::string_view text);

// 60 -> "C4", 61 -> "C#4".
std::string note_name(int note);

// Root note from a sample file name (PLAN.md §7.2): the last token that is a note name ("C3",
// "F#2") or a plain MIDI number 21..108 ("piano_60"). Tokens are split at _ - space . ( ) [ ].
std::optional<int> note_from_filename(std::string_view stem);

} // namespace phos
