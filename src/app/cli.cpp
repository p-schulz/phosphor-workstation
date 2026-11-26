#include "app/cli.h"

#include <string_view>

namespace phos::app {

const char* cli_usage() {
    return "Usage: phosphor [options]\n"
           "  (no options)                     open the workstation window\n"
           "  --headless --render-page <id> --out <file.png> [--project <dir>] [--theme <name>]\n"
           "             [--sample <file>] [--folder <dir>] [--inst <1-16>]\n"
           "                                   render one page without a window\n"
           "  --exec \"<command>\"               run a command after start (repeatable, one per frame)\n"
           "  --config-dir <dir>               use <dir> instead of the per-user settings folder\n"
           "  --size <w>x<h>                   initial window size (default 1024x768)\n"
           "  --list-devices                   list audio output devices\n"
           "  --analyze <file.wav>             peak, RMS, DC offset, spectral centroid\n"
           "  --render-test <file.wav> [--seconds <s>]\n"
           "                                   render the engine test sequence offline\n"
           "  --latency-report [--seconds <s>] [--stable|--immediate]\n"
           "                                   run audio + MIDI inputs without a window, print the\n"
           "                                   MIDI scheduling delay report (default 10 s)\n"
           "  --midi-loopback [--seconds <s>]  MIDI out -> in timing through a virtual port (macOS)\n"
           "  --render-audio <project> --out <file.wav> [--stems] [--float] [--rate <hz>]\n"
           "                                   render the song offline (24-bit, or 32-bit float)\n"
           "  --version, --help\n";
}

std::optional<CliOptions> parse_cli(int argc, char** argv, std::string* error) {
    CliOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) {
                *error = std::string(a) + " needs a value";
                return false;
            }
            out = argv[++i];
            return true;
        };
        bool ok = true;
        if (a == "--help" || a == "-h") {
            o.help = true;
        } else if (a == "--version") {
            o.version = true;
        } else if (a == "--headless") {
            o.headless = true;
        } else if (a == "--render-page") {
            ok = value(o.render_page);
        } else if (a == "--project") {
            ok = value(o.project);
        } else if (a == "--sample") {
            ok = value(o.sample);
        } else if (a == "--folder") {
            ok = value(o.folder);
        } else if (a == "--inst") {
            std::string v;
            ok = value(v);
            try {
                o.instrument = std::stoi(v);
            } catch (...) {
                ok = false;
            }
        } else if (a == "--view") {
            ok = value(o.view);
        } else if (a == "--out") {
            ok = value(o.out);
        } else if (a == "--theme") {
            ok = value(o.theme);
        } else if (a == "--exec") {
            std::string cmd;
            ok = value(cmd);
            o.exec.push_back(cmd);
        } else if (a == "--config-dir") {
            ok = value(o.config_dir);
        } else if (a == "--size") {
            std::string s;
            ok = value(s);
            const auto x = s.find_first_of("xX");
            try {
                o.width = std::stoi(s.substr(0, x));
                o.height = std::stoi(s.substr(x + 1));
            } catch (...) {
                *error = "--size expects <w>x<h>";
                return std::nullopt;
            }
        } else if (a == "--render-audio") {
            ok = value(o.render_audio);
        } else if (a == "--midi-loopback") {
            o.midi_loopback = true;
        } else if (a == "--stems") {
            o.stems = true;
        } else if (a == "--float") {
            o.float32 = true;
        } else if (a == "--rate") {
            std::string s;
            ok = value(s);
            try {
                o.rate = static_cast<unsigned>(std::stoul(s));
            } catch (...) {
                ok = false;
            }
            if (!ok || o.rate < 8000 || o.rate > 192000) {
                *error = "--rate expects 8000..192000";
                return std::nullopt;
            }
        } else if (a == "--analyze") {
            ok = value(o.analyze);
        } else if (a == "--render-test") {
            ok = value(o.render_test);
        } else if (a == "--seconds") {
            std::string s;
            ok = value(s);
            try {
                o.seconds = std::stod(s);
            } catch (...) {
                ok = false;
            }
            o.seconds_set = true;
            if (!ok || o.seconds <= 0.0 || o.seconds > 600.0) {
                *error = "--seconds expects 0..600";
                return std::nullopt;
            }
        } else if (a == "--stable") {
            o.placement = "STABLE";
        } else if (a == "--immediate") {
            o.placement = "IMMEDIATE";
        } else if (a == "--list-devices") {
            o.list_devices = true;
        } else if (a == "--latency-report") {
            o.latency_report = true;
        } else {
            *error = "unknown option " + std::string(a);
            return std::nullopt;
        }
        if (!ok) {
            return std::nullopt;
        }
    }
    if (!o.render_page.empty() && !o.headless) {
        o.headless = true; // --render-page never needs a window
    }
    if (o.headless && !o.render_page.empty() && o.out.empty()) {
        *error = "--render-page needs --out <file.png>";
        return std::nullopt;
    }
    return o;
}

} // namespace phos::app
