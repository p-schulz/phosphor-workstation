#pragma once

#include <optional>
#include <string>
#include <vector>

namespace phos::app {

struct CliOptions {
    bool help = false;
    bool version = false;
    bool headless = false;
    std::string render_page;   // --render-page <id>
    std::string project;       // --project <dir>
    std::string sample;        // --sample <file>: headless render with instrument 1 loaded
    std::string folder;        // --folder <dir>: headless Page 2 folder
    std::string view;          // --view <name>: headless page view (P3 OUT)
    int instrument = 0;        // --inst <1-16>: headless render with this instrument selected
    std::string out;           // --out <png|wav>
    std::string theme = "GREEN";
    std::vector<std::string> exec; // --exec "<command>" (repeatable), run in the window one per frame
    std::string config_dir;    // --config-dir <dir> overrides the settings folder
    int width = 1024;
    int height = 768;
    std::string analyze;       // --analyze <wav>
    bool list_devices = false;
    std::string render_test;   // --render-test <wav>
    double seconds = 8.0;      // --seconds <s> for --render-test
    bool latency_report = false; // --latency-report [--seconds s] [--stable|--immediate]
    bool midi_loopback = false;  // --midi-loopback [--seconds s]
    std::string placement;       // "", "STABLE" or "IMMEDIATE": overrides settings.json
    bool seconds_set = false;
    std::string render_audio;  // --render-audio <project> --out <wav> [--stems] [--float] [--rate <hz>]
    bool stems = false;
    bool float32 = false;
    unsigned rate = 48000;
};

std::optional<CliOptions> parse_cli(int argc, char** argv, std::string* error);
const char* cli_usage();

int run_headless(const CliOptions& options);
int run_windowed(const CliOptions& options);
int list_devices();
int analyze_file(const std::string& path);
int render_test(const std::string& out, double seconds);
int latency_report(const CliOptions& options);
int render_audio(const CliOptions& options);
int midi_loopback(const CliOptions& options);

} // namespace phos::app
