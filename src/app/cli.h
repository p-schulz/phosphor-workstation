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
    std::string out;           // --out <png|wav>
    std::string theme = "GREEN";
    std::vector<std::string> exec; // --exec "<command>" (repeatable), run in the window one per frame
    std::string config_dir;    // --config-dir <dir> overrides the settings folder
    int width = 1024;
    int height = 768;
    // Later milestones; recognised so scripts fail with a clear message.
    std::string render_audio;
    std::string analyze;
    bool list_devices = false;
    bool latency_report = false;
};

std::optional<CliOptions> parse_cli(int argc, char** argv, std::string* error);
const char* cli_usage();

int run_headless(const CliOptions& options);
int run_windowed(const CliOptions& options);

} // namespace phos::app
