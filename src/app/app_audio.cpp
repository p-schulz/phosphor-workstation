// Audio side of the windowed app: engine + device lifetime, Page S state, audio commands.

#include <algorithm>
#include <cstdio>
#include <string>

#include "app/app.h"
#include "core/rt_guard.h"
#include "core/text.h"
#include "platform/audio_miniaudio.h"

namespace phos::app {

namespace {

std::optional<std::uint32_t> parse_uint(const std::string& s) {
    if (s.empty() || s.size() > 7 || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::stoul(s));
}

} // namespace

void App::init_audio() {
    engine_ = std::make_unique<engine::Engine>();
    audio_ = std::make_unique<platform::MiniaudioBackend>();
    audio_info_.available = true;
    audio_info_.guard_enabled = rt::guard_enabled();
    open_audio();
}

void App::open_audio() {
    if (!audio_) {
        return;
    }
    audio_info_.devices.clear();
    for (const auto& d : audio_->list_devices()) {
        audio_info_.devices.push_back(d.name);
    }
    tone_note_ = -1; // a reopened engine starts silent
    audio_info_.tone_note = -1;
    const bool ok = audio_->open(settings_.audio, *engine_);
    const auto st = audio_->status();
    if (ok) {
        std::fprintf(stdout, "audio: %s, %u Hz, %u frames x %u%s\n", st.device.c_str(), st.sample_rate,
                     st.buffer_frames, st.periods, st.exclusive ? ", exclusive" : "");
    } else {
        std::fprintf(stdout, "audio: not running (%s)\n", st.error.c_str());
        if (ui_ && settings_.audio.enabled) {
            ui_->message("AUDIO: " + st.error, true);
        }
    }
    audio_info_.status = st;
}

void App::set_audio_config(const engine::AudioConfig& config) {
    settings_.audio = config;
    save_settings();
    open_audio();
}

void App::update_audio() {
    if (!engine_ || !audio_) {
        return;
    }
    engine_->collect_garbage();
    if (audio_->take_stopped_unexpectedly()) {
        ui_->message("AUDIO: DEVICE STOPPED. AUDIO RESTART TO REOPEN", true);
    }
    audio_info_.status = audio_->status();
    audio_info_.meters = engine_->read_meters();
    audio_info_.guard_violations = rt::violations();
    status_.cpu = audio_info_.meters.cpu;
    status_.voices = audio_info_.meters.voices;
}

void App::shutdown_audio() {
    if (!audio_) {
        return;
    }
    shutdown_midi(); // MIDI callbacks feed the engine: stop them first
    audio_->close();
    const auto m = engine_->read_meters();
    std::fprintf(stdout,
                 "audio stats: %llu callbacks, %llu frames, xruns %llu, overloads %llu, cpu peak %.1f%%, "
                 "guard %s (%llu violations)\n",
                 static_cast<unsigned long long>(m.callbacks), static_cast<unsigned long long>(m.frames),
                 static_cast<unsigned long long>(m.xruns), static_cast<unsigned long long>(m.overloads),
                 static_cast<double>(m.cpu_peak) * 100.0, rt::guard_enabled() ? "on" : "off",
                 static_cast<unsigned long long>(rt::violations()));
    audio_.reset();
    engine_.reset();
}

std::optional<std::string> App::audio_command(const ui::Command& cmd) {
    if (cmd.verb == "TONE") {
        if (cmd.args.empty()) {
            return std::string("ERROR: TONE <NOTE>|OFF");
        }
        if (cmd.arg_is(0, "OFF")) {
            if (tone_note_ >= 0) {
                engine_->send(engine::EngineCommand::note_off(tone_note_));
            }
            tone_note_ = -1;
            audio_info_.tone_note = -1;
            return std::string("TONE OFF");
        }
        const auto note = ui::parse_note(cmd.args[0]);
        if (!note) {
            return "ERROR: TONE: BAD NOTE " + to_upper(cmd.args[0]);
        }
        if (tone_note_ >= 0) {
            engine_->send(engine::EngineCommand::note_off(tone_note_));
        }
        tone_note_ = *note;
        audio_info_.tone_note = *note;
        engine_->send(engine::EngineCommand::note_on(engine::TestWave::Sine, *note, 0.8f));
        char buf[48];
        std::snprintf(buf, sizeof buf, "TONE %d (%.1f HZ)", *note, engine::VoicePool::note_hz(*note));
        return std::string(buf);
    }
    if (cmd.verb == "CLICK") {
        engine_->send(engine::EngineCommand::note_on(engine::TestWave::Click, 0, 1.0f));
        return std::string("CLICK");
    }
    if (cmd.verb == "MASTER") {
        float db = 0.0f;
        try {
            db = cmd.args.empty() ? 0.0f : std::stof(cmd.args[0]);
        } catch (...) {
            return std::string("ERROR: MASTER <DB>");
        }
        db = std::clamp(db, -60.0f, 6.0f);
        model::Song song = workspace_.doc().song;
        song.master_db = db;
        workspace_.set_song(song, "MASTER", time_s_); // sent to the engine by update_song()
        char buf[32];
        std::snprintf(buf, sizeof buf, "MASTER %.1f DB", static_cast<double>(db));
        return std::string(buf);
    }
    if (cmd.verb != "AUDIO") {
        return std::nullopt;
    }

    engine::AudioConfig cfg = settings_.audio;
    if (cmd.arg_is(0, "ON") || cmd.arg_is(0, "RESTART") || cmd.args.empty()) {
        cfg.enabled = true;
    } else if (cmd.arg_is(0, "OFF")) {
        cfg.enabled = false;
    } else if (cmd.arg_is(0, "NEXT")) {
        const auto& devs = audio_info_.devices;
        if (devs.empty()) {
            return std::string("ERROR: AUDIO: NO OUTPUT DEVICES");
        }
        const std::string& current = audio_info_.status.running ? audio_info_.status.device : cfg.device;
        const auto it = std::find(devs.begin(), devs.end(), current);
        const std::size_t next = it == devs.end() ? 0 : static_cast<std::size_t>(it - devs.begin() + 1) % devs.size();
        cfg.device = devs[next];
        cfg.enabled = true;
    } else if (cmd.arg_is(0, "BUFFER") || cmd.arg_is(0, "RATE")) {
        const auto v = cmd.args.size() > 1 ? parse_uint(cmd.args[1]) : std::nullopt;
        if (cmd.arg_is(0, "BUFFER")) {
            if (!v || *v < 32 || *v > 1024) {
                return std::string("ERROR: AUDIO BUFFER 32..1024");
            }
            cfg.buffer_frames = *v;
        } else {
            if (!v || *v < 8000 || *v > 192000) {
                return std::string("ERROR: AUDIO RATE 8000..192000");
            }
            cfg.sample_rate = *v;
        }
    } else if (cmd.arg_is(0, "EXCLUSIVE")) {
        cfg.exclusive = cmd.arg_is(1, "ON");
    } else {
        return "ERROR: " + std::string(ui::find_verb("AUDIO")->usage);
    }
    set_audio_config(cfg);
    const auto& st = audio_info_.status;
    if (!st.running) {
        return cfg.enabled ? "ERROR: AUDIO: " + st.error : std::string("AUDIO OFF");
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "AUDIO: %s %u HZ %u FR", fit(to_upper(st.device), 14).c_str(), st.sample_rate,
                  st.buffer_frames);
    return std::string(buf);
}

} // namespace phos::app
