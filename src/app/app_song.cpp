// Song side of the windowed app (M4): transport, recording into the song, position for the status
// line, mixer meters, projects (NEW / LOAD / SAVE / RECOVER) and autosave.

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "app/app.h"
#include "core/text.h"
#include "content/templates.h"
#include "io/project.h"
#include "seq/timing.h"

namespace phos::app {

namespace {

constexpr double kAutosaveIntervalS = 120.0; // PLAN.md §7.9
constexpr double kConfirmWindowS = 8.0;

} // namespace

std::filesystem::path App::projects_folder() const {
    if (!settings_.projects_folder.empty()) {
        return settings_.projects_folder;
    }
    const char* home = std::getenv(
#if defined(_WIN32)
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    return home ? std::filesystem::path(home) / "Documents" / "PHOSPHOR" : std::filesystem::path("PHOSPHOR");
}

std::filesystem::path App::autosave_bundle() const {
    return project_.bundle.empty() ? config_dir_ / "untitled.phosphor" : project_.bundle;
}

void App::init_song() {
    std::error_code ec;
    const auto untitled = config_dir_ / "untitled.phosphor" / io::kAutosaveFile;
    if (std::filesystem::exists(untitled, ec)) {
        ui_->message("UNSAVED WORK FROM LAST TIME: TYPE RECOVER TO RESTORE IT");
    }
    next_autosave_s_ = time_s_ + kAutosaveIntervalS;
}

void App::update_song() {
    if (!engine_) {
        return;
    }
    const engine::TransportInfo t = engine_->read_transport();
    const model::Song& song = workspace_.doc().song;
    const bool playing = t.state != seq::Sequencer::State::Stopped;
    status_.playing = playing;
    status_.recording = t.recording;
    status_.count_in = t.state == seq::Sequencer::State::CountIn;
    status_.section = playing ? t.section : song.current_section;
    status_.song_step = t.step;
    status_.pattern = playing ? t.pattern : song.section().pattern;
    status_.pattern_tick = t.tick;
    const auto& sec = song.sections[static_cast<std::size_t>(status_.section)];
    status_.bpm = sec.bpm10 / 10.0;
    status_.sig_num = sec.sig_num;
    status_.sig_den = sec.sig_den;
    const auto beat = static_cast<std::uint32_t>(seq::beat_ticks(sec.sig_den));
    const auto bar = static_cast<std::uint32_t>(seq::bar_ticks(sec.sig_num, sec.sig_den));
    status_.bar = static_cast<int>(t.tick / bar) + 1;
    status_.beat = static_cast<int>(t.tick % bar / beat) + 1;
    status_.tick = static_cast<int>(t.tick % beat);

    // Recording: a take starts with the first recorded pass and ends when recording stops.
    if (t.recording && !workspace_.in_take()) {
        workspace_.begin_take();
        open_notes_.clear();
    }
    engine::RecEvent r;
    while (engine_->pop_rec(r)) {
        add_recorded(r);
    }
    if (!t.recording && workspace_.in_take()) {
        close_open_notes(t.tick);
        workspace_.commit_take("RECORD");
        ui_->message("RECORDED (UNDO: MOD+Z)");
    }

    // Master level lives in the song (saved with the project).
    if (song.master_db != sent_master_db_) {
        sent_master_db_ = song.master_db;
        audio_info_.master_db = song.master_db;
        engine_->send(engine::EngineCommand::set_param(engine::EngineParam::MasterGainDb, song.master_db));
    }
    // Meters: peak hold with a ~0.3 s fall.
    const auto peaks = engine_->read_track_peaks();
    for (std::size_t b = 0; b < peaks.size(); ++b) {
        audio_info_.bus_peaks[b] = std::max(peaks[b], audio_info_.bus_peaks[b] * 0.93f);
    }

    project_.modified = workspace_.modified();
    status_.project = project_.name + (project_.modified ? "*" : "");
    if (time_s_ >= next_autosave_s_) {
        next_autosave_s_ = time_s_ + kAutosaveIntervalS;
        if (workspace_.revision() != autosaved_revision_ && project_.modified) {
            std::string error;
            if (io::save_project(autosave_bundle(), workspace_.doc(), &error, io::kAutosaveFile)) {
                autosaved_revision_ = workspace_.revision();
                std::fprintf(stdout, "autosave: %s\n", (autosave_bundle() / io::kAutosaveFile).string().c_str());
            } else {
                ui_->message("AUTOSAVE FAILED: " + to_upper(error), true);
            }
        }
    }
}

void App::add_recorded(const engine::RecEvent& r) {
    model::Song& song = workspace_.take_song();
    if (r.pattern < 0 || r.pattern >= static_cast<int>(song.patterns.size())) {
        return;
    }
    const int key = r.track * 128 + r.note;
    if (r.on) {
        open_notes_[key] = r;
        return;
    }
    const auto it = open_notes_.find(key);
    if (it == open_notes_.end()) {
        return;
    }
    const engine::RecEvent on = it->second;
    open_notes_.erase(it);
    model::Pattern& p = song.patterns[static_cast<std::size_t>(on.pattern)];
    const std::uint32_t len_ticks = p.length_ticks();
    std::uint32_t tick = on.tick;
    std::uint32_t length = r.tick >= on.tick ? r.tick - on.tick : len_ticks - on.tick + r.tick; // wrapped
    if (song.quantize_on_record) {
        const auto q = static_cast<std::uint32_t>(model::grid_ticks(song.record_quantize));
        tick = (tick + q / 2) / q * q % len_ticks;
        length = std::max(q, (length + q / 2) / q * q);
    }
    model::insert_note(p.lanes[on.track], model::Note{tick, std::max<std::uint32_t>(1, length), on.note, on.velocity});
}

void App::close_open_notes(std::uint32_t tick) {
    const auto open = open_notes_; // add_recorded() erases from the map
    for (const auto& [key, on] : open) {
        engine::RecEvent off = on;
        off.on = false;
        off.tick = tick;
        add_recorded(off);
    }
    open_notes_.clear();
}

bool App::confirm_discard(const std::string& verb) {
    if (!workspace_.modified() || (pending_confirm_ == verb && time_s_ - pending_confirm_s_ < kConfirmWindowS)) {
        pending_confirm_.clear();
        return true;
    }
    pending_confirm_ = verb;
    pending_confirm_s_ = time_s_;
    return false;
}

void App::open_document(model::Document doc, const std::filesystem::path& bundle, const std::string& name) {
    engine_->send(engine::EngineCommand::transport(engine::TransportAction::Stop));
    workspace_.replace(std::move(doc));
    project_.bundle = bundle;
    project_.name = to_upper(name);
    project_.modified = false;
    autosaved_revision_ = workspace_.revision();
    sent_master_db_ = 1e9f; // resend
}

std::optional<std::string> App::song_command(const ui::Command& cmd) {
    const auto send = [&](engine::TransportAction a) { engine_->send(engine::EngineCommand::transport(a)); };
    if (cmd.verb == "PLAY") {
        send(status_.playing ? engine::TransportAction::Stop : engine::TransportAction::Play);
        return std::string(status_.playing ? "STOP" : "PLAY");
    }
    if (cmd.verb == "STOP") {
        send(engine::TransportAction::Stop);
        return std::string("STOP");
    }
    if (cmd.verb == "CONTINUE") {
        send(engine::TransportAction::Continue);
        return std::string("CONTINUE");
    }
    if (cmd.verb == "REC") {
        send(engine::TransportAction::ToggleRecord);
        const auto& s = workspace_.doc().song;
        return status_.playing ? std::string(status_.recording ? "RECORD OFF" : "RECORD ON")
                               : "RECORD: COUNT-IN " + std::to_string(s.count_in_bars) + " BAR(S)";
    }
    if (cmd.verb == "NEW") {
        std::optional<model::Document> doc = model::Document{};
        if (!cmd.args.empty() && !content::is_template(cmd.joined_args())) {
            return std::string("ERROR: NEW [MALLSOFT|FUTURE-FUNK|CHIP-VAPOR]");
        }
        if (!confirm_discard("NEW")) {
            return std::string("ERROR: UNSAVED CHANGES. TYPE NEW AGAIN TO DISCARD THEM");
        }
        if (!cmd.args.empty()) {
            doc = content::make_template(cmd.joined_args(), engine_->sample_rate() ? engine_->sample_rate() : 48000);
        }
        open_document(std::move(*doc), {}, "UNTITLED");
        return cmd.args.empty() ? std::string("NEW PROJECT")
                                : "NEW " + to_upper(cmd.joined_args()) + ": SPACE PLAYS IT, VAPOR <0-100> ON PAGE L";
    }
    if (cmd.verb == "SAVE" || cmd.verb == "SAVEAS") {
        std::filesystem::path bundle = project_.bundle;
        if (!cmd.args.empty()) {
            bundle = io::bundle_path(projects_folder(), cmd.joined_args());
        } else if (cmd.verb == "SAVEAS" || bundle.empty()) {
            return std::string("ERROR: SAVE <NAME>");
        }
        std::string error;
        if (!io::save_project(bundle, workspace_.doc(), &error)) {
            return "ERROR: SAVE FAILED: " + to_upper(error);
        }
        if (project_.bundle.empty()) { // the untitled autosave is no longer needed
            std::error_code ec;
            std::filesystem::remove(config_dir_ / "untitled.phosphor" / io::kAutosaveFile, ec);
            std::filesystem::remove_all(config_dir_ / "untitled.phosphor" / io::kAutosaveDir, ec);
        }
        project_.bundle = bundle;
        project_.name = to_upper(bundle.stem().string());
        workspace_.mark_saved();
        autosaved_revision_ = workspace_.revision();
        std::fprintf(stdout, "save: %s\n", bundle.string().c_str());
        return "SAVED " + project_.name;
    }
    if (cmd.verb == "LOAD" || cmd.verb == "RECOVER") {
        const bool recover = cmd.verb == "RECOVER";
        if (!recover && cmd.args.empty()) {
            return std::string("ERROR: LOAD <NAME>");
        }
        if (!confirm_discard(cmd.verb)) {
            return "ERROR: UNSAVED CHANGES. TYPE " + cmd.verb + " AGAIN TO DISCARD THEM";
        }
        const std::filesystem::path bundle = recover ? autosave_bundle() : io::bundle_path(projects_folder(), cmd.joined_args());
        std::string error;
        auto doc = io::load_project(bundle, engine_->sample_rate() ? engine_->sample_rate() : 48000, &error,
                                    recover ? io::kAutosaveFile : io::kProjectFile);
        if (!doc && recover && project_.bundle.empty()) {
            doc = io::load_project(config_dir_ / "untitled.phosphor", engine_->sample_rate(), &error, io::kAutosaveFile);
        }
        if (!doc) {
            return "ERROR: " + to_upper(error);
        }
        const bool untitled = recover && project_.bundle.empty();
        open_document(std::move(*doc), untitled ? std::filesystem::path{} : bundle,
                      untitled ? "UNTITLED" : bundle.stem().string());
        if (recover) {
            workspace_.take_song(); // mark modified: the recovered state is not saved yet
            return std::string("RECOVERED THE AUTOSAVE. SAVE TO KEEP IT");
        }
        std::fprintf(stdout, "load: %s\n", bundle.string().c_str());
        return "LOADED " + project_.name;
    }
    return std::nullopt;
}

} // namespace phos::app
