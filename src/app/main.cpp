#include <cstdio>
#include <string>

#include "app/cli.h"
#include "core/version.h"

int main(int argc, char** argv) {
    using namespace phos::app;
    std::string error;
    const auto options = parse_cli(argc, argv, &error);
    if (!options) {
        std::fprintf(stderr, "error: %s\n\n%s", error.c_str(), cli_usage());
        return 2;
    }
    if (options->help) {
        std::fputs(cli_usage(), stdout);
        return 0;
    }
    if (options->version) {
        std::printf("%s %s\n", phos::product_name().data(), phos::version_string().data());
        return 0;
    }
    if (!options->render_audio.empty() || !options->analyze.empty()) {
        std::fprintf(stderr, "error: audio rendering and analysis arrive with the engine (M1/M8)\n");
        return 2;
    }
    if (options->list_devices || options->latency_report) {
        std::fprintf(stderr, "error: device listing and latency report arrive in M1/M2\n");
        return 2;
    }
    if (options->headless) {
        return run_headless(*options);
    }
    return run_windowed(*options);
}
