#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace phos::ui {

// A parsed command line. `verb` is uppercase; `args` keep their original spelling (paths).
struct Command {
    std::string verb;
    std::vector<std::string> args;

    bool arg_is(std::size_t i, std::string_view word) const; // case-insensitive
    std::string joined_args() const;
};

std::optional<Command> parse_command(std::string_view text);

// "P1", "PR", "ps" -> "1", "R", "S". Empty when `verb` is not a page command.
std::string page_id_from_verb(std::string_view verb);

struct VerbInfo {
    std::string_view verb;
    std::string_view usage;
    std::string_view summary;
    std::string_view milestone; // empty = available in this build
};
std::span<const VerbInfo> verb_table();

// "A4" -> 69, "C#3" -> 49, "Bb2" -> 46, "C-1" -> 0, "60" -> 60 (C4 = 60). nullopt if invalid.
std::optional<int> parse_note(std::string_view text);
const VerbInfo* find_verb(std::string_view verb);

// Tab completion: completes the verb (or a THEME / DISPLAY / SCALE argument). Returns the
// completed line, or the input unchanged when nothing matches.
std::string complete_command(std::string_view line);

} // namespace phos::ui
