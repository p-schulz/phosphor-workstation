// Loop side of the windowed app (M7): Page L slot state, STRETCH caches on a worker, loop launch /
// stop, LOOP <n> LOAD through the import pipeline, and RESAMPLE through the offline engine.

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "app/app.h"
#include "core/text.h"
#include "engine/offline.h"

namespace phos::app {

namespace {

template <typename T> bool done(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// "3" or "L3" -> loop slot 2; nullopt otherwise.
std::optional<int> slot_arg(const std::string& a) {
    std::string s = to_upper(a);
    if (!s.empty() && s[0] == 'L') {
        s = s.substr(1);
    }
    if (s.size() != 1 || s[0] < '1' || s[0] > '8') {
        return std::nullopt;
    }
    return s[0] - '1';
}

} // namespace

void App::update_loops() {
    if (!engine_) {
        return;
    }
    loop_info_ = engine_->read_loops();
    const model::Document& doc = workspace_.doc();

    // STRETCH: one cache at a time, for the tempo that is playing (or about to).
    if (done(stretch_job_)) {
        builder_.set_stretch(stretch_slot_, stretch_key_, stretch_job_.get());
        std::fprintf(stdout, "stretch: loop %d at %.1f BPM ready\n", stretch_slot_ + 1, stretch_key_.bpm10 / 10.0);
        publish_engine_snapshot();
    }
    if (!stretch_job_.valid()) {
        const int bpm10 = doc.song.sections[static_cast<std::size_t>(status_.section)].bpm10;
        for (int i = 0; i < model::kLoopSlots; ++i) {
            if (const auto key = builder_.stretch_wanted(doc, i, bpm10)) {
                stretch_slot_ = i;
                stretch_key_ = *key;
                const model::SampleRef src = doc.song.loops[static_cast<std::size_t>(i)].sample;
                const std::uint32_t rate = published_rate_ ? published_rate_ : 48000;
                stretch_job_ = std::async(std::launch::async,
                                          [src, k = *key, rate] { return engine::render_stretch(*src, k, rate); });
                break;
            }
        }
    }

    if (done(resample_job_)) {
        model::SampleRef r = resample_job_.get();
        import_status_.clear();
        if (resample_target_ < 100) {
            model::Song song = doc.song;
            model::LoopSlot& slot = song.loops[static_cast<std::size_t>(resample_target_)];
            slot.sample = r;
            slot.name = "RESAMPLE";
            const auto& pat = doc.song.pattern();
            slot.bars = std::max(1, static_cast<int>(pat.length_ticks() / (4 * model::kPpq)));
            slot.beats_per_bar = 4;
            workspace_.set_song(std::move(song), "RESAMPLE");
            ui_->message("RESAMPLED INTO LOOP " + std::to_string(resample_target_ + 1) + " (PAGE L)");
        } else {
            const int index = resample_target_ - 100;
            model::Instrument inst;
            inst.name = "RESAMPLE";
            inst.sample = r;
            workspace_.set_instrument(index, inst, "RESAMPLE");
            ui_->message("RESAMPLED INTO INSTRUMENT " + std::to_string(index + 1) + " (ROOT C4)");
        }
    }
}

std::optional<std::string> App::loop_command(const ui::Command& cmd) {
    if (cmd.verb == "RESAMPLE") {
        if (resample_job_.valid()) {
            return std::string("ERROR: RESAMPLE BUSY");
        }
        const model::Document& doc = workspace_.doc();
        int target = -1;
        if (cmd.args.empty()) {
            for (int i = 0; i < model::kLoopSlots && target < 0; ++i) {
                target = doc.song.loops[static_cast<std::size_t>(i)].sample ? -1 : i;
            }
            if (target < 0) {
                return std::string("ERROR: NO FREE LOOP SLOT. RESAMPLE L<1-8>|I<1-16>");
            }
        } else if (const auto slot = slot_arg(cmd.args[0])) {
            target = *slot;
        } else {
            const std::string a = to_upper(cmd.args[0]);
            const int n = a.size() > 1 && a[0] == 'I' ? std::atoi(a.c_str() + 1) : 0;
            if (n < 1 || n > model::kInstruments) {
                return std::string("ERROR: RESAMPLE [L<1-8>|I<1-16>]");
            }
            target = 100 + n - 1;
        }
        resample_target_ = target;
        engine::OfflineRender opt;
        opt.section = doc.song.current_section;
        opt.seamless = true;
        opt.master = false;
        opt.sample_rate = published_rate_ ? published_rate_ : 48000;
        opt.name = "RESAMPLE";
        import_status_ = "RESAMPLING SECTION " + std::string(1, static_cast<char>('A' + opt.section));
        resample_job_ = std::async(std::launch::async, [d = doc, opt] { return engine::render_section(d, opt); });
        return std::string("RESAMPLING ...");
    }
    if (cmd.verb != "LOOP" || cmd.args.size() < 2) {
        return std::nullopt;
    }
    const auto slot = slot_arg(cmd.args[0]);
    if (!slot) {
        return std::nullopt; // the UI's LOOP verbs report the usage
    }
    if (cmd.arg_is(1, "PLAY") || cmd.arg_is(1, "STOP") || cmd.arg_is(1, "TOGGLE")) {
        const int action = cmd.arg_is(1, "PLAY") ? 1 : cmd.arg_is(1, "STOP") ? 0 : 2;
        if (action != 0 && !workspace_.doc().song.loops[static_cast<std::size_t>(*slot)].sample) {
            return "ERROR: LOOP " + std::to_string(*slot + 1) + " IS EMPTY";
        }
        engine_->send(engine::EngineCommand::loop(*slot, action));
        return "LOOP " + std::to_string(*slot + 1) + (action == 1 ? " PLAY" : action == 0 ? " STOP" : " TOGGLE");
    }
    if (cmd.arg_is(1, "LOAD")) {
        if (cmd.args.size() < 3 || import_job_.valid()) {
            return std::string(import_job_.valid() ? "ERROR: BUSY: " + import_status_ : "ERROR: LOOP <N> LOAD <PATH>");
        }
        std::string path;
        for (std::size_t i = 2; i < cmd.args.size(); ++i) {
            path += (i > 2 ? " " : "") + cmd.args[i];
        }
        import_to_loop_ = true;
        import_sample(path, *slot);
        return "LOADING INTO LOOP " + std::to_string(*slot + 1);
    }
    return std::nullopt;
}

} // namespace phos::app
