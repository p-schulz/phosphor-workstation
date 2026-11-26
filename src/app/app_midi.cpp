// MIDI side of the windowed app: input ports (open, hotplug, reconnect by name), KEYJAZZ, CC learn,
// engine snapshots built from the MIDI settings, the latency report state.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#include "app/app.h"
#include "core/text.h"
#include "platform/midi_libremidi.h"
#include "seq/compiled_song.h"

namespace phos::app {

namespace {

constexpr double kScanIntervalS = 0.5;
constexpr double kActivityHoldS = 0.15;

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

void App::save_settings() {
    std::string error;
    if (!settings_.save(settings_path_, &error) && ui_) {
        ui_->message("ERROR: " + error, true);
    }
}

void App::init_midi() {
    midi_backend_ = std::make_unique<platform::LibremidiBackend>();
    midi_info_.available = true;
    midi_info_.api = to_upper(midi_backend_->api_name());
    scan_midi();
    // MIDI out (PLAN.md §7.7): messages leave at their frame's host time (from the engine's DLL)
    // plus the output latency compensation and the user offset.
    midi_out_thread_.start(
        [this](engine::TimedMidi& m) {
            engine::MidiOutEvent e;
            if (!engine_->pop_midi_out(e)) {
                return false;
            }
            double ns = 0.0;
            if (!engine_->frame_to_ns(e.frame, ns)) {
                ns = static_cast<double>(now_ns()); // clock not locked yet: send now
            }
            m.due_ns = ns + out_delay_ns_.load(std::memory_order_relaxed);
            m.slot = e.slot;
            m.size = e.size;
            m.bytes = e.bytes;
            return true;
        },
        [this](const engine::TimedMidi& m) { midi_backend_->send(m.slot, m.bytes.data(), m.size); });
    publish_engine_snapshot();
}

void App::send_all_notes_off() {
    for (int s = 0; s < engine::kMaxMidiOutSlots; ++s) {
        if (out_names_[static_cast<std::size_t>(s)].empty()) {
            continue;
        }
        for (int ch = 0; ch < 16; ++ch) {
            const std::uint8_t msg[3] = {static_cast<std::uint8_t>(0xB0 | ch), 123, 0};
            midi_backend_->send(s, msg, 3);
        }
    }
}

void App::set_midi_out_config(const ui::MidiOutConfig& config) {
    const bool was_on = settings_.midi_out.enabled;
    settings_.midi_out = config;
    save_settings();
    if (was_on && !config.enabled && midi_backend_) {
        send_all_notes_off(); // switching MIDI out off must not leave notes hanging
    }
    scan_midi_outputs();
    publish_engine_snapshot();
}

void App::scan_midi_outputs() {
    if (!midi_backend_) {
        return;
    }
    const auto names = midi_backend_->output_names();
    auto present = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    ui::MidiOutConfig& cfg = settings_.midi_out;
    bool cfg_changed = false;
    bool changed = false;
    for (const auto& n : names) {
        if (!cfg.find(n)) {
            cfg.outputs.push_back(ui::MidiOutputSetting{n});
            cfg_changed = true;
        }
    }
    for (int s = 0; s < engine::kMaxMidiOutSlots; ++s) {
        std::string& open = out_names_[static_cast<std::size_t>(s)];
        const ui::MidiOutputSetting* set = open.empty() ? nullptr : cfg.find(open);
        if (!open.empty() && (!cfg.enabled || !present(open) || !set || !set->enabled)) {
            midi_backend_->close_output(s);
            std::fprintf(stdout, "midi out: closed %s\n", open.c_str());
            open.clear();
            changed = true;
        }
    }
    out_errors_.clear();
    for (const auto& set : cfg.outputs) {
        const bool is_open = std::find(out_names_.begin(), out_names_.end(), set.name) != out_names_.end();
        if (!cfg.enabled || !set.enabled || is_open || !present(set.name)) {
            continue;
        }
        const auto free = std::find(out_names_.begin(), out_names_.end(), std::string{});
        std::string error;
        if (free == out_names_.end()) {
            out_errors_.emplace_back(set.name, "NO FREE SLOT");
        } else if (midi_backend_->open_output(set.name, static_cast<int>(free - out_names_.begin()), &error)) {
            *free = set.name;
            changed = true;
            std::fprintf(stdout, "midi out: opened %s (slot %d)\n", set.name.c_str(), static_cast<int>(free - out_names_.begin()));
        } else {
            out_errors_.emplace_back(set.name, to_upper(error));
        }
    }
    if (cfg_changed) {
        save_settings();
    }
    if (changed) {
        publish_engine_snapshot();
    }
    midi_info_.outputs.clear();
    for (int pass = 0; pass < 2; ++pass) { // present outputs first
        for (const auto& set : cfg.outputs) {
            if (present(set.name) != (pass == 0)) {
                continue;
            }
            ui::MidiOutPortState st;
            st.name = set.name;
            st.present = pass == 0;
            st.enabled = set.enabled;
            st.clock = set.clock;
            st.open = std::find(out_names_.begin(), out_names_.end(), set.name) != out_names_.end();
            for (const auto& [n, e] : out_errors_) {
                st.error = n == set.name ? e : st.error;
            }
            midi_info_.outputs.push_back(st);
        }
    }
}

void App::shutdown_midi() {
    if (!midi_backend_) {
        return;
    }
    midi_out_thread_.stop();
    send_all_notes_off(); // PANIC on close (PLAN.md §7.7)
    for (int s = 0; s < engine::kMaxMidiOutSlots; ++s) {
        midi_backend_->close_output(s);
    }
    for (int s = 0; s < engine::kMaxMidiSlots; ++s) {
        midi_backend_->close_input(s);
    }
    midi_backend_.reset();
    const auto m = engine_->read_midi_meters();
    const auto l = engine_->latency();
    std::fprintf(stdout,
                 "midi stats: %llu events, %llu note-ons, lost %llu, early %llu, late %llu; "
                 "delay avg %.3f ms, min %.3f, max %.3f (%llu timed)\n",
                 static_cast<unsigned long long>(m.events), static_cast<unsigned long long>(m.note_ons),
                 static_cast<unsigned long long>(m.queue_overflow), static_cast<unsigned long long>(m.schedule_overflow),
                 static_cast<unsigned long long>(m.late), l.avg_ms, l.min_ms, l.max_ms,
                 static_cast<unsigned long long>(l.count));
}

void App::scan_midi() {
    if (!midi_backend_) {
        return;
    }
    scan_midi_outputs();
    const auto names = midi_backend_->input_names();
    auto present = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    ui::MidiConfig& cfg = settings_.midi;
    bool cfg_changed = false;
    bool slots_changed = false;

    for (const auto& n : names) {
        if (!cfg.find(n)) {
            cfg.inputs.push_back(ui::MidiInputSetting{n});
            cfg_changed = true;
        }
    }
    // Close inputs that went away or were switched off.
    for (int s = 0; s < engine::kMaxMidiSlots; ++s) {
        std::string& open = slot_names_[static_cast<std::size_t>(s)];
        if (open.empty()) {
            continue;
        }
        const ui::MidiInputSetting* set = cfg.find(open);
        if (!present(open) || !set || !set->enabled) {
            midi_backend_->close_input(s);
            if (!present(open) && ui_) {
                ui_->message("MIDI: " + to_upper(open) + " DISCONNECTED");
            }
            std::fprintf(stdout, "midi: closed %s\n", open.c_str());
            open.clear();
            slots_changed = true;
        }
    }
    // Open enabled inputs that are present and not open yet (also after a reconnect).
    for (const auto& n : names) {
        const ui::MidiInputSetting* set = cfg.find(n);
        const bool is_open = std::find(slot_names_.begin(), slot_names_.end(), n) != slot_names_.end();
        if (!set || !set->enabled || is_open) {
            continue;
        }
        const auto free = std::find(slot_names_.begin(), slot_names_.end(), std::string{});
        std::string error;
        midi_errors_.erase(std::remove_if(midi_errors_.begin(), midi_errors_.end(),
                                          [&](const auto& e) { return e.first == n; }),
                           midi_errors_.end());
        if (free == slot_names_.end()) {
            midi_errors_.emplace_back(n, "NO FREE SLOT");
            continue;
        }
        const int slot = static_cast<int>(free - slot_names_.begin());
        if (midi_backend_->open_input(n, slot, engine_->midi_queues(), &error)) {
            *free = n;
            slots_changed = true;
            std::fprintf(stdout, "midi: opened %s (slot %d)\n", n.c_str(), slot);
            if (midi_scanned_ && ui_) {
                ui_->message("MIDI: " + to_upper(n) + " CONNECTED");
            }
        } else {
            midi_errors_.emplace_back(n, to_upper(error));
        }
    }
    midi_scanned_ = true;
    if (cfg_changed) {
        save_settings();
    }
    if (slots_changed) {
        publish_engine_snapshot();
    }

    midi_info_.ports.clear();
    for (int pass = 0; pass < 2; ++pass) { // present inputs first, then remembered ones
        for (const auto& set : cfg.inputs) {
            const bool here = present(set.name);
            if (here != (pass == 0)) {
                continue;
            }
            ui::MidiPortState st;
            st.name = set.name;
            st.present = here;
            st.enabled = set.enabled;
            st.channel = set.channel;
            st.open = std::find(slot_names_.begin(), slot_names_.end(), set.name) != slot_names_.end();
            for (const auto& [name, err] : midi_errors_) {
                if (name == set.name) {
                    st.error = err;
                }
            }
            midi_info_.ports.push_back(st);
        }
    }
}

void App::publish_engine_snapshot() {
    if (!engine_) {
        return;
    }
    published_rate_ = engine_->sample_rate() ? engine_->sample_rate() : 48000;
    auto s = builder_.build(workspace_.doc(), published_rate_);
    add_audition(*s);
    const ui::MidiConfig& m = settings_.midi;
    s->serial = ++snapshot_serial_;
    s->placement = m.placement;
    s->velocity_curve = m.velocity_curve;
    s->bend_range_semitones = m.bend_range;
    for (int slot = 0; slot < engine::kMaxMidiSlots; ++slot) {
        const auto* set = m.find(slot_names_[static_cast<std::size_t>(slot)]);
        s->slot_channel[static_cast<std::size_t>(slot)] = static_cast<std::int8_t>(set ? set->channel : -1);
    }
    published_revision_ = workspace_.revision();
    // MIDI out: tracks without a port name use the first open output.
    const ui::MidiOutConfig& mo = settings_.midi_out;
    int first_open = -1;
    for (int k = 0; k < engine::kMaxMidiOutSlots; ++k) {
        const std::string& n = out_names_[static_cast<std::size_t>(k)];
        if (n.empty()) {
            continue;
        }
        first_open = first_open < 0 ? k : first_open;
        s->open_slots |= static_cast<std::uint8_t>(1u << k);
        if (const auto* set = mo.find(n); set && set->clock) {
            s->clock_slots |= static_cast<std::uint8_t>(1u << k);
        }
    }
    s->midi_out = mo.enabled && first_open >= 0;
    s->thru = mo.thru;
    for (int t = 0; t < model::kTracks; ++t) {
        const std::string& port = workspace_.doc().song.tracks[static_cast<std::size_t>(t)].out_port;
        int slot = port.empty() ? first_open : -1;
        for (int k = 0; k < engine::kMaxMidiOutSlots && slot < 0; ++k) {
            slot = out_names_[static_cast<std::size_t>(k)] == port ? k : -1;
        }
        s->track_out_slot[static_cast<std::size_t>(t)] = static_cast<std::int8_t>(slot);
    }
    if (m.master_cc >= 0) {
        s->cc_bindings[0] = engine::CcBinding{static_cast<std::uint8_t>(m.master_cc),
                                              static_cast<std::uint8_t>(m.master_cc_channel < 0 ? 0xFF : m.master_cc_channel),
                                              engine::EngineParam::MasterGainDb};
        s->cc_binding_count = 1;
    }
    if (!engine_->publish(std::move(s)) && ui_) {
        ui_->message("ERROR: ENGINE BUSY, SETTING NOT APPLIED", true);
    }
}

void App::update_midi() {
    if (!midi_backend_ || !engine_) {
        return;
    }
    if (time_s_ >= next_midi_scan_s_) {
        scan_midi();
        next_midi_scan_s_ = time_s_ + kScanIntervalS;
    }
    midi_info_.meters = engine_->read_midi_meters();
    // MIDI out timing: output latency (when compensated) + the user offset.
    const auto& ast = audio_info_.status;
    const double latency_ns = settings_.midi_out.latency_comp && ast.running && ast.sample_rate
                                  ? 1e9 * ast.buffer_frames * std::max(1u, ast.periods) / ast.sample_rate
                                  : 0.0;
    out_delay_ns_.store(latency_ns + settings_.midi_out.offset_ms * 1e6, std::memory_order_relaxed);
    midi_info_.out_sent = midi_out_thread_.sent();
    midi_info_.out_worst_late_ms = midi_out_thread_.worst_late_ns() / 1e6;
    midi_info_.out_lost = engine_->midi_out_overflow();
    midi_info_.latency = engine_->latency();
    if (midi_info_.meters.events != last_midi_events_) {
        last_midi_events_ = midi_info_.meters.events;
        midi_activity_until_s_ = time_s_ + kActivityHoldS;
    }
    midi_info_.activity = time_s_ < midi_activity_until_s_;
    status_.midi_activity = midi_info_.activity;

    engine::CcEvent cc;
    while (engine_->pop_cc(cc)) {
        midi_info_.last_cc = "CC " + std::to_string(cc.cc) + " = " + std::to_string(cc.value) + "  CH " +
                             std::to_string(cc.channel + 1);
        if (midi_info_.learning) {
            midi_info_.learning = false;
            settings_.midi.master_cc = cc.cc;
            settings_.midi.master_cc_channel = cc.channel;
            save_settings();
            publish_engine_snapshot();
            ui_->message("MIDI LEARN: MASTER = CC " + std::to_string(cc.cc) + " CH " + std::to_string(cc.channel + 1));
        }
    }
}

void App::set_midi_config(const ui::MidiConfig& config) {
    settings_.midi = config;
    save_settings();
    scan_midi();
    publish_engine_snapshot();
}

void App::keyjazz_note(int note, bool on) {
    if (!engine_) {
        return;
    }
    const auto vel = static_cast<std::uint8_t>(on ? settings_.midi.keyjazz_velocity : 0);
    engine_->send(engine::EngineCommand::midi_message(on ? 0x90 : 0x80, static_cast<std::uint8_t>(note), vel, now_ns()));
}

void App::reset_latency() {
    if (engine_) {
        engine_->reset_latency();
    }
}

void App::learn_master_cc(bool on) { midi_info_.learning = on; }

std::optional<std::string> App::midi_command(const ui::Command& cmd) {
    if (cmd.verb == "PANIC") {
        engine_->send(engine::EngineCommand::panic());
        if (auditioning_) {
            auditioning_ = false;
        }
        return std::string("PANIC: ALL SOUND OFF");
    }
    if (cmd.verb != "MIDI") {
        return std::nullopt;
    }
    if (cmd.arg_is(0, "RESCAN") || cmd.args.empty()) {
        scan_midi();
        int open = 0;
        for (const auto& p : midi_info_.ports) {
            open += p.open ? 1 : 0;
        }
        return "MIDI: " + std::to_string(open) + " INPUT" + (open == 1 ? "" : "S") + " OPEN";
    }
    if (cmd.arg_is(0, "LEARN")) {
        if (cmd.arg_is(1, "CLEAR")) {
            settings_.midi.master_cc = -1;
            save_settings();
            publish_engine_snapshot();
            return std::string("MIDI LEARN: MASTER UNBOUND");
        }
        learn_master_cc(!midi_info_.learning);
        return std::string(midi_info_.learning ? "MIDI LEARN: MOVE A CONTROLLER FOR MASTER" : "MIDI LEARN CANCELLED");
    }
    if (cmd.arg_is(0, "RESET")) {
        reset_latency();
        return std::string("MIDI: LATENCY REPORT RESET");
    }
    return "ERROR: " + std::string(ui::find_verb("MIDI")->usage);
}

} // namespace phos::app
