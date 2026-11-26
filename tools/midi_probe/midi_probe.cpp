// midi_probe: test source for PHOSPHOR's MIDI input. Creates a virtual MIDI output called
// "PHOSPHOR PROBE" (CoreMIDI; not available on WinMM), waits for PHOSPHOR to connect, then sends
// a burst of notes as fast as possible or a series of timed notes.
//
//   midi_probe --burst 10000              10 000 note-on/off pairs back to back
//   midi_probe --notes 200 --interval 50  200 notes, one every 50 ms (latency report input)
//   midi_probe --listen 10                virtual input 'PHOSPHOR PROBE IN' for 10 s: prints what
//                                         PHOSPHOR's MIDI out sends and the clock tick spacing
//   options: --wait <s> (default 3), --note <0-127> (default 60), --hold <s> after sending (default 1)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

#include <libremidi/libremidi.hpp>

namespace {

int usage() {
    std::fputs("usage: midi_probe [--wait s] (--burst N | --notes N [--interval ms] | --listen s) [--note n] [--hold s]\n",
               stderr);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    double wait_s = 3.0;
    double hold_s = 1.0;
    long burst = 0;
    long notes = 0;
    long interval_ms = 50;
    double listen_s = 0.0;
    int note = 60;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (i + 1 >= argc) {
            return usage();
        }
        const char* v = argv[++i];
        if (a == "--wait") {
            wait_s = std::atof(v);
        } else if (a == "--hold") {
            hold_s = std::atof(v);
        } else if (a == "--burst") {
            burst = std::atol(v);
        } else if (a == "--notes") {
            notes = std::atol(v);
        } else if (a == "--interval") {
            interval_ms = std::atol(v);
        } else if (a == "--listen") {
            listen_s = std::atof(v);
        } else if (a == "--note") {
            note = std::atoi(v) & 0x7F;
        } else {
            return usage();
        }
    }
    if (listen_s > 0.0) {
        // Receive side: count message kinds, keep clock tick times (system timestamps, ns).
        std::mutex m;
        std::vector<std::int64_t> clocks;
        long counts[8] = {};
        int shown = 0;
        libremidi::input_configuration conf;
        conf.timestamps = libremidi::timestamp_mode::SystemMonotonic;
        conf.ignore_timing = false; // keep MIDI clock (libremidi drops it by default)
        conf.on_message = [&](const libremidi::message& msg) {
            std::lock_guard lock(m);
            if (msg.bytes.empty()) {
                return;
            }
            const unsigned char st = msg.bytes[0];
            if (st == 0xF8) {
                clocks.push_back(msg.timestamp);
                return;
            }
            counts[(st >> 4) & 7]++;
            if (shown < 24) {
                ++shown;
                std::printf("  %02X", st);
                for (std::size_t i = 1; i < msg.bytes.size(); ++i) {
                    std::printf(" %02X", msg.bytes[i]);
                }
                std::printf("\n");
            }
        };
        libremidi::midi_in in(conf);
        if (auto err = in.open_virtual_port("PHOSPHOR PROBE IN"); err != stdx::error{}) {
            std::fputs("midi_probe: cannot create the virtual input\n", stderr);
            return 1;
        }
        std::printf("midi_probe: virtual input 'PHOSPHOR PROBE IN' listening for %.1f s\n", listen_s);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::duration<double>(listen_s));
        std::lock_guard lock(m);
        std::printf("midi_probe: note-on %ld, note-off %ld, cc %ld, program %ld, clock ticks %zu\n", counts[1], counts[0],
                    counts[3], counts[4], clocks.size());
        if (clocks.size() > 2) {
            std::vector<double> d;
            for (std::size_t i = 1; i < clocks.size(); ++i) {
                d.push_back(static_cast<double>(clocks[i] - clocks[i - 1]) / 1e6);
            }
            double mean = 0.0;
            for (const double x : d) {
                mean += x;
            }
            mean /= static_cast<double>(d.size());
            const auto [lo, hi] = std::minmax_element(d.begin(), d.end());
            std::printf("midi_probe: clock interval mean %.3f ms (%.1f BPM), min %.3f, max %.3f, spread %.3f ms\n", mean,
                        60000.0 / (mean * 24.0), *lo, *hi, *hi - *lo);
        }
        return 0;
    }
    if (burst <= 0 && notes <= 0) {
        return usage();
    }

    libremidi::midi_out out;
    if (auto err = out.open_virtual_port("PHOSPHOR PROBE"); err != stdx::error{}) {
        std::fputs("midi_probe: cannot create the virtual port\n", stderr);
        return 1;
    }
    std::printf("midi_probe: virtual output 'PHOSPHOR PROBE' open, waiting %.1f s\n", wait_s);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::duration<double>(wait_s));

    const auto n = static_cast<unsigned char>(note);
    const auto t0 = std::chrono::steady_clock::now();
    long sent = 0;
    if (burst > 0) {
        for (long i = 0; i < burst; ++i) {
            out.send_message(0x90, n, 100);
            out.send_message(0x80, n, 0);
            ++sent;
        }
    } else {
        for (long i = 0; i < notes; ++i) {
            out.send_message(0x90, n, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms / 2));
            out.send_message(0x80, n, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms - interval_ms / 2));
            ++sent;
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("midi_probe: sent %ld note-on/off pairs in %.1f ms\n", sent, ms);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::duration<double>(hold_s));
    return 0;
}
