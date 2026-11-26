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
    if (!options->render_audio.empty()) {
        return render_audio(*options);
    }
    if (options->midi_loopback) {
        return midi_loopback(*options);
    }
    if (options->latency_report) {
        return latency_report(*options);
    }
    if (options->list_devices) {
        return list_devices();
    }
    if (!options->analyze.empty()) {
        return analyze_file(options->analyze);
    }
    if (!options->render_test.empty()) {
        return render_test(options->render_test, options->seconds);
    }
    if (options->headless) {
        return run_headless(*options);
    }
    return run_windowed(*options);
}
