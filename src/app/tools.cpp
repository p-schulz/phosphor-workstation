// Command-line tools without a window: --list-devices, --analyze, --render-test.

#include <algorithm>
#include <memory>
#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "app/cli.h"
#include "dsp/analysis.h"
#include "engine/engine.h"
#include "io/project.h"
#include "io/render_export.h"
#include "io/wav.h"
#include "platform/audio_miniaudio.h"
#include "platform/midi_libremidi.h"
#include "platform/midi_out_thread.h"
#include "platform/paths.h"
#include "ui/settings.h"

namespace phos::app {

int list_devices() {
    platform::MiniaudioBackend backend;
    const auto devices = backend.list_devices();
    if (devices.empty()) {
        std::fprintf(stdout, "no audio output devices (%s)\n", backend.status().error.c_str());
        return 1;
    }
    std::fprintf(stdout, "audio outputs:\n");
    for (const auto& d : devices) {
        std::fprintf(stdout, "  %s %s\n", d.is_default ? "*" : " ", d.name.c_str());
    }
    std::fprintf(stdout, "midi inputs/outputs: arrive in M2\n");
    return 0;
}

int analyze_file(const std::string& path) {
    std::string error;
    const auto buf = io::read_wav(path, &error);
    if (!buf) {
        std::fprintf(stderr, "error: %s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    const auto a = dsp::analyze(buf->samples, buf->channels, buf->sample_rate);
    std::fprintf(stdout, "file:       %s\n", path.c_str());
    std::fprintf(stdout, "format:     %u Hz, %u ch, %zu frames (%.3f s)\n", buf->sample_rate, buf->channels, a.frames,
                 static_cast<double>(a.frames) / buf->sample_rate);
    for (std::size_t c = 0; c < a.channels.size(); ++c) {
        const auto& ch = a.channels[c];
        std::fprintf(stdout, "ch %zu:       peak %7.2f dBFS  rms %7.2f dBFS  dc %+.6f\n", c + 1, ch.peak_db, ch.rms_db,
                     ch.dc_offset);
    }
    std::fprintf(stdout, "centroid:   %.1f Hz\n", a.centroid_hz);
    return 0;
}

// Deterministic test render through the engine's realtime code path (offline clock): a slow
// arpeggio of sine notes with clicks on each beat, then a 32-voice full-velocity cluster that must
// be caught by the master limiter.
int render_test(const std::string& out, double seconds) {
    constexpr std::uint32_t kRate = 48000;
    constexpr std::uint32_t kBlock = 256;
    engine::Engine eng;
    eng.prepare(kRate, 2);
    const auto total = static_cast<std::uint32_t>(seconds * kRate);
    io::AudioBuffer buf;
    buf.sample_rate = kRate;
    buf.channels = 2;
    buf.samples.resize(static_cast<std::size_t>(total) * 2);

    constexpr int kArp[] = {60, 64, 67, 72};
    const std::uint32_t beat = kRate / 2; // 120 BPM
    const std::uint32_t cluster_at = total * 3 / 4;
    int last_note = -1;
    for (std::uint32_t pos = 0; pos < total; pos += kBlock) {
        if (pos % beat < kBlock && pos < cluster_at) {
            const auto step = static_cast<int>(pos / beat);
            if (last_note >= 0) {
                eng.send(engine::EngineCommand::note_off(last_note));
            }
            last_note = kArp[step % 4];
            eng.send(engine::EngineCommand::note_on(engine::TestWave::Sine, last_note, 0.8f));
            eng.send(engine::EngineCommand::note_on(engine::TestWave::Click, 0, step % 4 == 0 ? 1.0f : 0.5f));
        }
        if (pos <= cluster_at && cluster_at < pos + kBlock) {
            eng.send(engine::EngineCommand::all_notes_off());
            for (int v = 0; v < 32; ++v) {
                eng.send(engine::EngineCommand::note_on(engine::TestWave::Sine, 48 + v, 1.0f));
            }
        }
        const std::uint32_t n = std::min(kBlock, total - pos);
        eng.render_offline(buf.samples.data() + static_cast<std::size_t>(pos) * 2, n, 2);
    }
    std::string error;
    if (!io::write_wav(out, buf, io::WavFormat::Float32, &error)) {
        std::fprintf(stderr, "error: %s: %s\n", out.c_str(), error.c_str());
        return 1;
    }
    std::fprintf(stdout, "wrote %s (%.1f s, 48000 Hz, float32)\n", out.c_str(), seconds);
    return 0;
}

// MIDI out -> MIDI in loopback (PLAN.md M8 AC: jitter < 1 ms on macOS). A virtual output is read
// back through an input with system timestamps; notes are sent through the real MIDI-out thread
// at known due times, every 20 ms with a varying fraction of a millisecond added. Reports when
// each note arrived relative to its due time.
int midi_loopback(const CliOptions& o) {
    const double seconds = o.seconds_set ? o.seconds : 10.0;
    const char* kPort = "PHOSPHOR LOOPBACK";
    platform::LibremidiBackend midi;
    std::string error;
    if (!midi.open_virtual_output(kPort, 0, &error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    auto queues = std::make_unique<engine::MidiInputQueues>();
    bool opened = false;
    for (int i = 0; i < 40 && !opened; ++i) { // the observer needs a moment to see the new port
        const auto names = midi.input_names();
        if (std::find(names.begin(), names.end(), kPort) != names.end()) {
            opened = midi.open_input(kPort, 0, *queues, &error);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    if (!opened) {
        std::fprintf(stderr, "error: cannot read back %s (%s)\n", kPort, error.c_str());
        return 1;
    }
    const auto now = [] {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
    };
    const int count = static_cast<int>(seconds * 50.0);
    std::vector<double> due(static_cast<std::size_t>(count));
    const double t0 = now() + 200e6;
    for (int i = 0; i < count; ++i) {
        due[static_cast<std::size_t>(i)] = t0 + i * 20e6 + (i * 7919 % 1000) * 1e3; // + 0..1 ms
    }
    std::atomic<int> next{0};
    platform::MidiOutThread thread;
    thread.start(
        [&](engine::TimedMidi& m) {
            const int i = next.load(std::memory_order_relaxed);
            if (i >= count) {
                return false;
            }
            next.store(i + 1, std::memory_order_relaxed);
            m.due_ns = due[static_cast<std::size_t>(i)];
            m.slot = 0;
            m.size = 3;
            m.bytes = {0x90, static_cast<std::uint8_t>(i & 0x7F), static_cast<std::uint8_t>(1 + (i >> 7))};
            return true;
        },
        [&](const engine::TimedMidi& m) { midi.send(m.slot, m.bytes.data(), m.size); });
    std::fprintf(stdout, "midi loopback: %d notes over %.0f s through %s (%s)\n", count, seconds, kPort,
                 midi.api_name().c_str());
    std::fflush(stdout);
    std::vector<double> delay; // ms
    const double end = t0 + count * 20e6 + 500e6;
    engine::MidiInEvent e;
    while (now() < end) {
        while (queues->pop(0, e)) {
            if ((e.status & 0xF0) == 0x90) {
                const int i = e.data1 | (e.data2 - 1) << 7;
                if (i >= 0 && i < count) {
                    delay.push_back((static_cast<double>(e.ns) - due[static_cast<std::size_t>(i)]) / 1e6);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    thread.stop();
    midi.close_input(0);
    midi.close_output(0);
    if (delay.empty()) {
        std::fprintf(stderr, "error: nothing came back\n");
        return 1;
    }
    std::sort(delay.begin(), delay.end());
    double sum = 0.0;
    for (const double d : delay) {
        sum += d;
    }
    const double mean = sum / static_cast<double>(delay.size());
    double var = 0.0;
    for (const double d : delay) {
        var += (d - mean) * (d - mean);
    }
    const double sd = std::sqrt(var / static_cast<double>(delay.size()));
    const auto pct = [&](double p) { return delay[static_cast<std::size_t>(p * static_cast<double>(delay.size() - 1))]; };
    std::fprintf(stdout, "received:   %zu of %d\n", delay.size(), count);
    std::fprintf(stdout, "delay:      mean %.3f ms, min %.3f, median %.3f, p99 %.3f, max %.3f\n", mean, delay.front(),
                 pct(0.5), pct(0.99), delay.back());
    std::fprintf(stdout, "jitter:     %.3f ms peak-to-peak, %.3f ms standard deviation\n", delay.back() - delay.front(), sd);
    std::fprintf(stdout, "result:     %s (M8 AC: jitter < 1 ms)\n", delay.back() - delay.front() < 1.0 ? "PASS" : "FAIL");
    return delay.back() - delay.front() < 1.0 ? 0 : 3;
}

int render_audio(const CliOptions& o) {
    if (o.out.empty()) {
        std::fprintf(stderr, "error: --render-audio needs --out <file.wav>\n");
        return 2;
    }
    std::string error;
    const auto doc = io::load_project(o.render_audio, o.rate, &error);
    if (!doc) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    io::ExportOptions opt;
    opt.stems = o.stems;
    opt.float32 = o.float32;
    opt.sample_rate = o.rate;
    const io::ExportResult r = io::export_audio(*doc, o.out, opt);
    if (!r.ok) {
        std::fprintf(stderr, "error: %s\n", r.error.c_str());
        return 1;
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    for (const auto& f : r.files) {
        std::fprintf(stdout, "wrote %s\n", f.string().c_str());
    }
    std::fprintf(stdout, "%.2f s of audio at %u Hz, %s, rendered in %.2f s (%.0fx real time)\n", r.seconds, o.rate,
                 o.float32 ? "32-bit float" : "24-bit", took, took > 0 ? r.seconds / took : 0.0);
    return 0;
}

// Audio + MIDI without a window: opens the configured device and every enabled MIDI input
// (rescanning twice a second, so inputs plugged in meanwhile join), then prints the scheduling
// delay report (PLAN.md §7.6).
int latency_report(const CliOptions& o) {
    const std::filesystem::path dir = o.config_dir.empty() ? platform::default_config_dir()
                                                           : std::filesystem::path(o.config_dir);
    ui::Settings settings;
    std::string error;
    if (std::filesystem::exists(dir / "settings.json")) {
        if (auto s = ui::Settings::load(dir / "settings.json", &error)) {
            settings = std::move(*s);
        }
    }
    if (o.placement == "STABLE") {
        settings.midi.placement = engine::Placement::Stable;
    } else if (o.placement == "IMMEDIATE") {
        settings.midi.placement = engine::Placement::Immediate;
    }
    const double seconds = o.seconds_set ? o.seconds : 10.0;

    engine::Engine eng;
    auto snap = std::make_unique<engine::EngineSnapshot>();
    snap->placement = settings.midi.placement;
    snap->velocity_curve = settings.midi.velocity_curve;
    snap->bend_range_semitones = settings.midi.bend_range;
    eng.publish(std::move(snap));

    platform::MiniaudioBackend audio;
    settings.audio.enabled = true;
    if (!audio.open(settings.audio, eng)) {
        std::fprintf(stderr, "error: audio: %s\n", audio.status().error.c_str());
        return 1;
    }
    const auto st = audio.status();
    platform::LibremidiBackend midi;
    std::vector<std::string> slots(engine::kMaxMidiSlots);
    auto scan = [&] {
        for (const auto& name : midi.input_names()) {
            const auto* set = settings.midi.find(name);
            if ((set && !set->enabled) || std::find(slots.begin(), slots.end(), name) != slots.end()) {
                continue;
            }
            const auto free = std::find(slots.begin(), slots.end(), std::string{});
            if (free != slots.end() && midi.open_input(name, static_cast<int>(free - slots.begin()), eng.midi_queues(), &error)) {
                *free = name;
                std::fprintf(stdout, "midi: listening to %s\n", name.c_str());
                std::fflush(stdout);
            }
        }
    };
    std::fprintf(stdout, "latency report: %s, %u Hz, %u frames x %u, %s, %.0f s\n", st.device.c_str(), st.sample_rate,
                 st.buffer_frames, st.periods,
                 settings.midi.placement == engine::Placement::Stable ? "STABLE" : "IMMEDIATE", seconds);
    std::fflush(stdout);
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        scan();
        eng.collect_garbage();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    for (int s = 0; s < engine::kMaxMidiSlots; ++s) {
        midi.close_input(s);
    }
    audio.close();

    const auto m = eng.read_meters();
    const auto mm = eng.read_midi_meters();
    const auto l = eng.latency();
    const double block_ms = st.sample_rate ? 1000.0 * m.last_block / st.sample_rate : 0.0;
    std::fprintf(stdout, "block:      %u frames = %.3f ms (device period %u frames x %u)\n", m.last_block, block_ms,
                 st.buffer_frames, st.periods);
    std::fprintf(stdout, "events:     %llu timed, %llu handled, %llu note-ons\n", static_cast<unsigned long long>(l.count),
                 static_cast<unsigned long long>(mm.events), static_cast<unsigned long long>(mm.note_ons));
    std::fprintf(stdout, "delay:      avg %.3f ms, min %.3f, max %.3f, jitter %.3f ms\n", l.avg_ms, l.min_ms, l.max_ms,
                 l.jitter_ms());
    std::fprintf(stdout, "lost:       queue %llu (early: scheduler full %llu, late %llu); xruns %llu, overloads %llu\n",
                 static_cast<unsigned long long>(mm.queue_overflow), static_cast<unsigned long long>(mm.schedule_overflow),
                 static_cast<unsigned long long>(mm.late), static_cast<unsigned long long>(m.xruns),
                 static_cast<unsigned long long>(m.overloads));
    std::fprintf(stdout, "histogram:  (0.25 ms per bucket)\n");
    for (int b = 0; b < engine::LatencyStats::kBuckets; ++b) {
        const auto n = l.histogram[static_cast<std::size_t>(b)];
        if (n == 0) {
            continue;
        }
        const double lo = b * 0.25;
        std::fprintf(stdout, "  %5.2f-%5.2f ms %7llu %s\n", lo, lo + 0.25, static_cast<unsigned long long>(n),
                     std::string(static_cast<std::size_t>(std::min<std::uint64_t>(60, n * 60 / std::max<std::uint64_t>(1, l.count))), '#')
                         .c_str());
    }
    return 0;
}

} // namespace phos::app
