#include "ui/commands.h"

#include <array>

#include "core/notes.h"
#include "core/text.h"
#include "gfx/palette.h"

namespace phos::ui {

namespace {

constexpr std::array<VerbInfo, 57> kVerbs = {{
    {"P", "P<id>", "GO TO PAGE (P1, PR, PS ...)", ""},
    {"HELP", "HELP", "HELP FOR THE CURRENT PAGE", ""},
    {"PLAY", "PLAY", "START / STOP PLAYBACK (SPACE)", ""},
    {"STOP", "STOP", "STOP PLAYBACK", ""},
    {"CONTINUE", "CONTINUE", "CONTINUE FROM THE STOP POSITION", ""},
    {"REC", "REC", "RECORD (COUNT-IN WHEN STOPPED)", ""},
    {"MODE", "MODE PATTERN|SONG", "LOOP THE SECTION OR PLAY THE SONG", ""},
    {"TEMPO", "TEMPO <BPM>", "SECTION TEMPO, 20.0-300.0", ""},
    {"SIG", "SIG <N>/<D>", "SECTION TIME SIGNATURE", ""},
    {"SWING", "SWING <50-75> [1/8|1/16]", "SWING FOR TRACKS THAT OPT IN", ""},
    {"TAP", "TAP", "TAP TEMPO", ""},
    {"METRO", "METRO OFF|PLAY|REC|COUNT <0-2>", "METRONOME AND COUNT-IN", ""},
    {"QUANTIZE", "QUANTIZE OFF|<GRID>", "QUANTIZE ON RECORD", ""},
    {"TRACK", "TRACK 1-8|A-H", "SELECT THE TRACK (AND ITS INSTRUMENT)", ""},
    {"NEW", "NEW [MALLSOFT|FUTURE-FUNK|CHIP-VAPOR]", "NEW PROJECT, EMPTY OR FROM A TEMPLATE", ""},
    {"LOAD", "LOAD <NAME>", "LOAD PROJECT", ""},
    {"SAVE", "SAVE [NAME]", "SAVE PROJECT", ""},
    {"RECOVER", "RECOVER", "LOAD THE LAST AUTOSAVE", ""},
    {"UNDO", "UNDO", "UNDO THE LAST EDIT", ""},
    {"REDO", "REDO", "REDO", ""},
    {"IMPORT", "IMPORT <PATH|NAME>", "SAMPLE / .PHINST INTO THE INSTRUMENT; .MID: SONG", ""},
    {"INST", "INST <1-16>", "SELECT THE CURRENT INSTRUMENT", ""},
    {"KIND", "KIND SAMPLER|WAVE|SYNTH|DRUM|GB|SNES", "INSTRUMENT TYPE (NEW DEFAULTS)", ""},
    {"COMPUTE", "COMPUTE [RAW]", "HARMONICS -> WAVEFORMS (PAGE 4)", ""},
    {"MASTER", "MASTER <DB>", "MASTER LEVEL, -60 TO +6 DB", ""},
    {"AUDIO", "AUDIO ON|OFF|NEXT|BUFFER <N>|RATE <HZ>", "AUDIO DEVICE, BUFFER, RATE", ""},
    {"MIDI", "MIDI RESCAN|LEARN|RESET", "RESCAN, CC LEARN, LATENCY RESET", ""},
    {"KEYJAZZ", "KEYJAZZ ON|OFF|OCTAVE <N>", "COMPUTER KEYBOARD PLAYS NOTES", ""},
    {"TONE", "TONE <NOTE>|OFF", "TEST TONE, E.G. TONE A4 OR TONE 69", ""},
    {"CLICK", "CLICK", "TEST CLICK", ""},
    {"THEME", "THEME GREEN|AMBER|GREY|VAPOR|CYAN|COLOR", "SET THE PHOSPHOR COLOUR", ""},
    {"PAGEKEY", "PAGEKEY OFF|CTRL|ALT|CMD", "MODIFIER + PAGE ID JUMPS TO A PAGE", ""},
    {"FOLLOW", "FOLLOW [ON|OFF]", "PAGE R FOLLOWS THE PLAYHEAD WHILE PLAYING", ""},
    {"DISPLAY", "DISPLAY NEXT|FULL|WINDOW|TOGGLE", "MOVE / FULLSCREEN THE DISPLAY", ""},
    {"SCALE", "SCALE FIT|INTEGER|STRETCH", "CANVAS SCALING MODE", ""},
    {"CRT", "CRT ON|OFF", "CRT POST PASS ON / OFF", ""},
    {"TESTCARD", "TESTCARD", "SHOW THE GEOMETRY TEST CARD", ""},
    {"SNAP", "SNAP [FILE.PNG]", "SAVE THE CANVAS AS PNG", ""},
    {"SHOT", "SHOT [FILE.PNG]", "SAVE THE SCREEN (WITH CRT) AS PNG", ""},
    {"WAIT", "WAIT <FRAMES>", "PAUSE A --exec SCRIPT", ""},
    {"QUIT", "QUIT", "LEAVE PHOSPHOR", ""},
    {"RENDER", "RENDER [STEMS] [FLOAT] [PATH.WAV]", "RENDER THE SONG TO WAV (24-BIT)", ""},
    {"EXPORT", "EXPORT MIDI [PATH] | INST [NAME]", "SONG AS SMF / INSTRUMENT TO THE LIBRARY", ""},
    {"LIBRARY", "LIBRARY", "INSTRUMENT LIBRARY ON PAGE 2", ""},
    {"VAPOR", "VAPOR <0-100>", "VAPOR MACRO: SLOWER, DARKER, WETTER, WOBBLIER", ""},
    {"LOOP", "LOOP <1-8> PLAY|STOP|LOAD|FROM|BARS|SYNC", "LOOP SLOTS (PAGE L); ALSO CLEAR, QUANT", ""},
    {"RESAMPLE", "RESAMPLE [L<1-8>|I<1-16>]", "RENDER THE SECTION INTO A LOOP OR INSTRUMENT", ""},
    {"FX", "FX 1|2|M1|M2|RA|RB <TYPE> [VALUES]", "INSERT / MASTER / RETURN EFFECT", ""},
    {"SEND", "SEND A|B <0-100>", "TRACK SEND TO RETURN A / B", ""},
    {"RETURN", "RETURN A|B <0-100>", "RETURN LEVEL", ""},
    {"TRACKTYPE", "TRACKTYPE NOTES|LOOPS", "TRACK PLAYS NOTES OR LAUNCHES LOOPS", ""},
    {"TRACKOUT", "TRACKOUT INT|EXT|BOTH [CH N] [PROG N]", "WHERE THE TRACK PLAYS (PAGE 3 OUT)", ""},
    {"MIDIOUT", "MIDIOUT ON|OFF|THRU|OFFSET|PORT|CLOCK", "MIDI OUTPUT SETTINGS (PAGE 3 OUT)", ""},
    {"PANIC", "PANIC", "ALL SOUND OFF NOW (MOD+ESC)", ""},
    {"SAVEAS", "SAVEAS <NAME>", "SAVE UNDER A NEW NAME", ""},
    {"SECTION", "SECTION A-Z", "SELECT THE SECTION EDITED ON PAGE R", ""},
    {"PATTERN", "PATTERN <1-64>|NEW|COPY", "PATTERN OF THE CURRENT SECTION", ""},
}};

constexpr std::array<std::string_view, 4> kDisplayArgs = {"NEXT", "FULL", "WINDOW", "TOGGLE"};
constexpr std::array<std::string_view, 3> kScaleArgs = {"FIT", "INTEGER", "STRETCH"};
constexpr std::array<std::string_view, 2> kOnOff = {"ON", "OFF"};
constexpr std::array<std::string_view, 6> kAudioArgs = {"ON", "OFF", "RESTART", "NEXT", "BUFFER", "RATE"};
constexpr std::array<std::string_view, 3> kKeyjazzArgs = {"ON", "OFF", "OCTAVE"};
constexpr std::array<std::string_view, 3> kMidiArgs = {"RESCAN", "LEARN", "RESET"};
constexpr std::array<std::string_view, 4> kMetroArgs = {"OFF", "PLAY", "REC", "COUNT"};
constexpr std::array<std::string_view, 2> kModeArgs = {"PATTERN", "SONG"};
constexpr std::array<std::string_view, 3> kNewArgs = {"MALLSOFT", "FUTURE-FUNK", "CHIP-VAPOR"};
constexpr std::array<std::string_view, 2> kTrackTypeArgs = {"NOTES", "LOOPS"};
constexpr std::array<std::string_view, 4> kPageKeyArgs = {"OFF", "CTRL", "ALT", "CMD"};

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

std::optional<int> parse_note(std::string_view text) { return phos::parse_note_name(text); }

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
    } else if (verb == "AUDIO") {
        done = complete_word(partial, kAudioArgs);
    } else if (verb == "KEYJAZZ") {
        done = complete_word(partial, kKeyjazzArgs);
    } else if (verb == "MIDI") {
        done = complete_word(partial, kMidiArgs);
    } else if (verb == "METRO") {
        done = complete_word(partial, kMetroArgs);
    } else if (verb == "MODE") {
        done = complete_word(partial, kModeArgs);
    } else if (verb == "NEW") {
        done = complete_word(partial, kNewArgs);
    } else if (verb == "TRACKTYPE") {
        done = complete_word(partial, kTrackTypeArgs);
    } else if (verb == "PAGEKEY") {
        done = complete_word(partial, kPageKeyArgs);
    }
    if (done.empty()) {
        return std::string(line);
    }
    return verb + " " + done;
}

} // namespace phos::ui
