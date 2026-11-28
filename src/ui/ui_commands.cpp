// Ui::run_ui_command: the verbs the UI handles itself (pages, views, undo, files, library,
// MIDI import / export, settings, display). Document edits of the mixer and the sequencer live in
// mix_commands.cpp and song_commands.cpp; verbs needing the engine are the host's (app.cpp).

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include "content/gm.h"
#include "core/text.h"
#include "io/instrument_io.h"
#include "io/smf.h"
#include "ui/keyjazz.h"
#include "ui/page_catalog.h"
#include "ui/song_commands.h"
#include "ui/ui.h"

namespace phos::ui {

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
    if (cmd.verb == "THEME" || cmd.verb == "SCALE" || cmd.verb == "CRT" || cmd.verb == "SIDEKEYS") {
        DisplayProfile prof = app_.profile();
        if (cmd.args.empty()) {
            if (cmd.verb == "THEME") {
                std::string names;
                for (const gfx::Theme& t : gfx::all_themes()) {
                    names += " " + std::string(t.name);
                }
                message("THEMES:" + names);
                return std::string{};
            }
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
        } else if (cmd.verb == "SIDEKEYS") {
            const auto mode = gfx::parse_side_keys(cmd.args[0]);
            if (!mode) {
                message("SIDEKEYS: OFF, AUTO OR ON", true);
                return std::string{};
            }
            prof.side_keys = *mode;
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

} // namespace phos::ui
