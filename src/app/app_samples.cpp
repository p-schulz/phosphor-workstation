// Sample side of the windowed app: imports and auditions on worker threads, instruments in the
// engine snapshot, the Page 2 folder.

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "app/app.h"
#include "core/notes.h"
#include "core/text.h"

namespace phos::app {

namespace {

constexpr std::size_t kRamWarningBytes = std::size_t{1} << 30; // PLAN.md §7.2: warn above 1 GB

template <typename T> bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

io::ImportOptions options_for(const engine::Engine* e) {
    io::ImportOptions o;
    if (e && e->sample_rate() != 0) {
        o.target_rate = e->sample_rate();
    }
    return o;
}

} // namespace

void App::import_sample(const std::filesystem::path& path, int index) {
    if (import_job_.valid()) {
        ui_->message("BUSY: " + import_status_, true);
        return;
    }
    import_index_ = index;
    import_status_ = "IMPORTING " + to_upper(path.filename().string());
    import_job_ = std::async(std::launch::async, io::import_sample, path, options_for(engine_.get()));
}

void App::audition_file(const std::filesystem::path& path) {
    if (auditioning_) {
        engine_->send(engine::EngineCommand::sampler_release(engine::kAuditionSlot));
        auditioning_ = false;
    }
    if (!audition_job_.valid()) {
        audition_job_ = std::async(std::launch::async, io::import_sample, path, options_for(engine_.get()));
    }
}

void App::audition_instrument(int index, bool on) {
    const model::Instrument& inst = workspace_.doc().instruments[static_cast<std::size_t>(index)];
    if (on && inst.sample) {
        engine_->send(engine::EngineCommand::sampler_note_on(index, inst.sampler.root_note, 1.0f));
    } else {
        engine_->send(engine::EngineCommand::sampler_release(index));
    }
}

void App::update_samples() {
    if (ready(import_job_)) {
        io::ImportResult r = import_job_.get();
        import_status_.clear();
        const bool to_loop = import_to_loop_;
        import_to_loop_ = false;
        if (!r.ok()) {
            ui_->message("IMPORT FAILED: " + to_upper(r.error), true);
        } else if (to_loop) {
            // Page L: the loop's length in bars is guessed from the current tempo (editable).
            model::Song song = workspace_.doc().song;
            model::LoopSlot& slot = song.loops[static_cast<std::size_t>(import_index_)];
            slot.sample = r.sample;
            slot.name = to_upper(r.sample->name);
            slot.beats_per_bar = 4;
            slot.bars = model::detect_bars(r.sample->seconds(), song.section().bpm10 / 10.0);
            workspace_.set_song(std::move(song), "LOOP LOAD");
            char buf[96];
            std::snprintf(buf, sizeof buf, "LOOP %d: %s, %d BAR(S) = %.1f BPM", import_index_ + 1,
                          fit(slot.name, 12).c_str(), slot.bars, slot.loop_bpm());
            ui_->message(buf);
        } else {
            model::Instrument inst;
            inst.name = to_upper(r.sample->name);
            inst.sample = r.sample;
            inst.sampler = r.params;
            workspace_.set_instrument(import_index_, inst, "IMPORT");
            workspace_.select(import_index_);
            ui_->message("IMPORTED " + fit(inst.name, 12) + " ROOT " + note_name(r.params.root_note) + " (" +
                         r.root_source + ")" + (r.params.loop != model::LoopMode::Off ? " LOOP" : ""));
            std::fprintf(stdout, "import: %s -> instrument %d, %s, root %s (%s)\n", r.sample->source_path.c_str(),
                         import_index_ + 1, r.description.c_str(), note_name(r.params.root_note).c_str(),
                         r.root_source.c_str());
            std::size_t bytes = 0;
            for (const auto& i : workspace_.doc().instruments) {
                bytes += i.sample ? i.sample->bytes() : 0;
            }
            if (bytes > kRamWarningBytes) {
                ui_->message("WARNING: SAMPLES USE " + std::to_string(bytes >> 20) + " MB", true);
            }
        }
    }
    if (ready(audition_job_)) {
        audition_ = audition_job_.get();
        if (!audition_.ok()) {
            ui_->message("AUDITION FAILED: " + to_upper(audition_.error), true);
        } else {
            publish_engine_snapshot(); // carries the audition sample in its slot
            engine_->send(engine::EngineCommand::sampler_note_on(engine::kAuditionSlot, audition_.params.root_note, 1.0f));
            auditioning_ = true;
        }
    }
    if (workspace_.revision() != published_revision_ ||
        (engine_ && engine_->sample_rate() != 0 && engine_->sample_rate() != published_rate_)) {
        publish_engine_snapshot(); // a new device rate also needs effect units for that rate
    }
}

void App::add_audition(engine::EngineSnapshot& s) const {
    if (audition_.ok()) {
        model::SamplerParams p = audition_.params;
        p.loop = model::LoopMode::Off; // play the file once
        s.instruments[engine::kAuditionSlot] = engine::sampler_block(audition_.sample.get(), p);
        s.sample_refs[engine::kAuditionSlot] = audition_.sample;
    }
}

std::filesystem::path App::browse_folder() const {
    std::error_code ec;
    if (!settings_.browse_folder.empty() && std::filesystem::is_directory(settings_.browse_folder, ec)) {
        return settings_.browse_folder;
    }
    const char* home = std::getenv(
#if defined(_WIN32)
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    if (!home) {
        return std::filesystem::current_path(ec);
    }
    const std::filesystem::path music = std::filesystem::path(home) / "Music";
    return std::filesystem::is_directory(music, ec) ? music : std::filesystem::path(home);
}

void App::set_browse_folder(const std::filesystem::path& folder) {
    if (settings_.browse_folder != folder.string()) {
        settings_.browse_folder = folder.string();
        save_settings();
    }
}

} // namespace phos::app
