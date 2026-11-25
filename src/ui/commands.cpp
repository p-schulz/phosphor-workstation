#include "ui/commands.h"

#include <array>

#include "core/text.h"
#include "gfx/palette.h"

namespace phos::ui {

namespace {

constexpr std::array<VerbInfo, 26> kVerbs = {{
    {"P", "P<id>", "GO TO PAGE (P1, PR, PS ...)", ""},
    {"HELP", "HELP", "HELP FOR THE CURRENT PAGE", ""},
    {"THEME", "THEME GREEN|AMBER|GREY|VAPOR|CYAN", "SET THE PHOSPHOR COLOUR", ""},
    {"DISPLAY", "DISPLAY NEXT|FULL|WINDOW|TOGGLE", "MOVE / FULLSCREEN THE DISPLAY", ""},
    {"SCALE", "SCALE FIT|INTEGER|STRETCH", "CANVAS SCALING MODE", ""},
    {"CRT", "CRT ON|OFF", "CRT POST PASS ON / OFF", ""},
    {"TESTCARD", "TESTCARD", "SHOW THE GEOMETRY TEST CARD", ""},
    {"SNAP", "SNAP [FILE.PNG]", "SAVE THE CANVAS AS PNG", ""},
    {"SHOT", "SHOT [FILE.PNG]", "SAVE THE SCREEN (WITH CRT) AS PNG", ""},
    {"WAIT", "WAIT <FRAMES>", "PAUSE A --exec SCRIPT", ""},
    {"QUIT", "QUIT", "LEAVE PHOSPHOR", ""},
    {"NEW", "NEW [TEMPLATE]", "NEW PROJECT", "M4"},
    {"LOAD", "LOAD <NAME>", "LOAD PROJECT", "M4"},
    {"SAVE", "SAVE [NAME]", "SAVE PROJECT", "M4"},
    {"IMPORT", "IMPORT <PATH>", "IMPORT A SAMPLE", "M3"},
    {"TEMPO", "TEMPO <BPM>", "SET TEMPO", "M4"},
    {"SIG", "SIG <N>/<D>", "SET TIME SIGNATURE", "M4"},
    {"SWING", "SWING <50-75>", "SET SWING", "M4"},
    {"PLAY", "PLAY", "START PLAYBACK", "M4"},
    {"STOP", "STOP", "STOP PLAYBACK", "M4"},
    {"REC", "REC", "RECORD", "M4"},
    {"RENDER", "RENDER [STEMS]", "RENDER TO WAV", "M8"},
    {"VAPOR", "VAPOR <0-100>", "VAPOR MACRO", "M7"},
    {"PANIC", "PANIC", "ALL NOTES OFF", "M8"},
    {"UNDO", "UNDO", "UNDO", "M4"},
    {"REDO", "REDO", "REDO", "M4"},
}};

constexpr std::array<std::string_view, 4> kDisplayArgs = {"NEXT", "FULL", "WINDOW", "TOGGLE"};
constexpr std::array<std::string_view, 3> kScaleArgs = {"FIT", "INTEGER", "STRETCH"};
constexpr std::array<std::string_view, 2> kOnOff = {"ON", "OFF"};

// Longest common completion of `prefix` among candidates; empty if none match.
template <typename Range> std::string complete_word(std::string_view prefix, const Range& candidates) {
    std::string best;
    bool any = false;
    for (const auto& cand : candidates) {
        const std::string_view c = cand;
        if (!istarts_with(c, prefix)) {
            continue;
        }
        if (!any) {
            best = std::string(c);
            any = true;
            continue;
        }
        std::size_t n = 0;
        while (n < best.size() && n < c.size() && best[n] == c[n]) {
            ++n;
        }
        best.resize(n);
    }
    return best;
}

} // namespace

bool Command::arg_is(std::size_t i, std::string_view word) const { return i < args.size() && iequals(args[i], word); }

std::string Command::joined_args() const {
    std::string out;
    for (const auto& a : args) {
        if (!out.empty()) {
            out += ' ';
        }
        out += a;
    }
    return out;
}

std::optional<Command> parse_command(std::string_view text) {
    auto words = split_words(trim(text));
    if (words.empty()) {
        return std::nullopt;
    }
    Command cmd;
    cmd.verb = to_upper(words.front());
    cmd.args.assign(words.begin() + 1, words.end());
    return cmd;
}

std::string page_id_from_verb(std::string_view verb) {
    if (verb.size() < 2 || (verb[0] != 'P' && verb[0] != 'p')) {
        return {};
    }
    // Page ids are a single character; longer words starting with P (PLAY, PANIC) are verbs.
    return verb.size() == 2 ? to_upper(verb.substr(1)) : std::string{};
}

std::span<const VerbInfo> verb_table() { return kVerbs; }

const VerbInfo* find_verb(std::string_view verb) {
    for (const auto& v : kVerbs) {
        if (iequals(v.verb, verb)) {
            return &v;
        }
    }
    return nullptr;
}

std::string complete_command(std::string_view line) {
    const auto words = split_words(line);
    const bool trailing_space = !line.empty() && (line.back() == ' ' || line.back() == '\t');
    if (words.empty()) {
        return std::string(line);
    }
    if (words.size() == 1 && !trailing_space) {
        std::vector<std::string_view> verbs;
        for (const auto& v : kVerbs) {
            if (v.verb != "P") {
                verbs.push_back(v.verb);
            }
        }
        const std::string done = complete_word(words[0], verbs);
        if (done.empty()) {
            return std::string(line);
        }
        const bool unique = find_verb(done) != nullptr;
        return done + (unique ? " " : "");
    }
    if (words.size() > 2 || (words.size() == 2 && trailing_space)) {
        return std::string(line);
    }
    const std::string verb = to_upper(words[0]);
    const std::string_view partial = words.size() == 2 ? std::string_view(words[1]) : std::string_view{};
    std::string done;
    if (verb == "THEME") {
        std::vector<std::string_view> names;
        for (const auto& t : gfx::all_themes()) {
            names.push_back(t.name);
        }
        done = complete_word(partial, names);
    } else if (verb == "DISPLAY") {
        done = complete_word(partial, kDisplayArgs);
    } else if (verb == "SCALE") {
        done = complete_word(partial, kScaleArgs);
    } else if (verb == "CRT") {
        done = complete_word(partial, kOnOff);
    }
    if (done.empty()) {
        return std::string(line);
    }
    return verb + " " + done;
}

} // namespace phos::ui
