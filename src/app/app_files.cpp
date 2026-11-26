// RENDER (M8): the song through the offline engine into WAV files on a worker thread.

#include <chrono>
#include <cstdio>

#include "app/app.h"
#include "core/text.h"

namespace phos::app {

void App::update_files() {
    if (!render_job_.valid() || render_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const io::ExportResult r = render_job_.get();
    import_status_.clear();
    if (!r.ok) {
        ui_->message("RENDER FAILED: " + to_upper(r.error), true);
        return;
    }
    for (const auto& f : r.files) {
        std::fprintf(stdout, "render: %s\n", f.string().c_str());
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "RENDERED %.1f S: %s%s", r.seconds, to_upper(r.files.front().filename().string()).c_str(),
                  r.files.size() > 1 ? (" + " + std::to_string(r.files.size() - 1) + " STEMS").c_str() : "");
    ui_->message(buf);
}

std::optional<std::string> App::files_command(const ui::Command& cmd) {
    if (cmd.verb != "RENDER") {
        return std::nullopt;
    }
    if (render_job_.valid()) {
        return std::string("ERROR: RENDER BUSY");
    }
    io::ExportOptions opt;
    opt.sample_rate = engine_ && engine_->sample_rate() ? engine_->sample_rate() : 48000;
    std::string path;
    for (const auto& a : cmd.args) {
        if (iequals(a, "STEMS")) {
            opt.stems = true;
        } else if (iequals(a, "FLOAT")) {
            opt.float32 = true;
        } else {
            path += (path.empty() ? "" : " ") + a;
        }
    }
    std::filesystem::path out =
        path.empty() ? projects_folder() / "Exports" / (project_.name + ".wav") : std::filesystem::path(path);
    if (out.extension().empty()) {
        out += ".wav";
    }
    import_status_ = "RENDERING " + to_upper(out.filename().string());
    render_job_ = std::async(std::launch::async, [doc = workspace_.doc(), out, opt] { return io::export_audio(doc, out, opt); });
    return "RENDERING " + to_upper(out.filename().string()) + (opt.stems ? " + STEMS" : "") + " ...";
}

} // namespace phos::app
