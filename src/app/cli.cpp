#include "app/cli.h"

#include <string_view>

namespace phos::app {

const char* cli_usage() {
    return "Usage: phosphor [options]\n"
           "  (no options)                     open the workstation window\n"
           "  --headless --render-page <id> --out <file.png> [--project <dir>] [--theme <name>]\n"
           "                                   render one page without a window\n"
           "  --exec \"<command>\"               run a command after start (repeatable, one per frame)\n"
           "  --config-dir <dir>               use <dir> instead of the per-user settings folder\n"
           "  --size <w>x<h>                   initial window size (default 1024x768)\n"
           "  --render-audio <project> --out <file.wav>   (M8)\n"
           "  --analyze <file.wav>             (M1)\n"
           "  --list-devices, --latency-report (M1 / M2)\n"
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
        } else if (a == "--analyze") {
            ok = value(o.analyze);
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
