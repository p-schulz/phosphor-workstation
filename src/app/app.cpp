#include "app/app.h"

#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "core/text.h"
#include "core/version.h"
#include "gfx/image_io.h"
#include "gfx/viewport.h"
#include "platform/paths.h"
#include "ui/layout.h"
#include "ui/render.h"

namespace phos::app {

namespace {

constexpr bool kIsMac =
#if defined(__APPLE__)
    true;
#else
    false;
#endif

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

App::App(CliOptions options)
    : options_(std::move(options)), canvas_(ui::kCanvasW, ui::kCanvasH), screen_(ui::kCanvasW, ui::kCanvasH) {
    exec_.assign(options_.exec.begin(), options_.exec.end());
}

App::~App() {
    shutdown_audio();
    // The UI references this context; destroy it before the members it uses.
    ui_.reset();
}

int run_windowed(const CliOptions& options) {
    App app(options);
    return app.run();
}

void App::load_settings() {
    config_dir_ = options_.config_dir.empty() ? platform::default_config_dir()
                                               : std::filesystem::path(options_.config_dir);
    profiles_path_ = config_dir_ / "display_profiles.json";
    settings_path_ = config_dir_ / "settings.json";
    std::string error;
    if (auto loaded = ui::DisplayProfiles::load(profiles_path_, &error)) {
        profiles_ = std::move(*loaded);
    } else if (auto example = ui::DisplayProfiles::load(platform::exe_dir() / "conf" / "display_profiles.example.json",
                                                        &error)) {
        profiles_ = std::move(*example);
    }

    if (std::filesystem::exists(settings_path_)) {
        if (auto loaded = ui::Settings::load(settings_path_, &error)) {
            settings_ = std::move(*loaded);
        } else {
            std::fprintf(stderr, "settings.json ignored: %s\n", error.c_str());
        }
    }

    keys_ = ui::KeyMap::defaults(kIsMac);
    if (const auto text = read_file(config_dir_ / "keys.cfg")) {
        if (auto map = ui::KeyMap::parse(*text, kIsMac, &error)) {
            keys_ = std::move(*map);
        } else {
            std::fprintf(stderr, "keys.cfg ignored: %s\n", error.c_str());
        }
    }
}

bool App::init() {
    load_settings();
    std::string error;
    fonts_ = ui::load_fonts(platform::exe_dir() / "assets", &error);
    if (!fonts_) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return false;
    }
    const std::string title = std::string(product_name()) + " " + std::string(version_string());
    if (!window_.create(title.c_str(), options_.width, options_.height, &error) ||
        !crt_.init(platform::exe_dir() / "shaders", ui::kCanvasW, ui::kCanvasH, &error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return false;
    }
    for (const auto& m : window_.monitors()) {
        known_monitors_.push_back(m.name);
        std::fprintf(stdout, "monitor: %s %dx%d@%d at %d,%d\n", m.name.c_str(), m.mode_w, m.mode_h, m.refresh_hz, m.x,
                     m.y);
    }
    if (known_monitors_.empty()) {
        std::fprintf(stdout, "monitor: none reported by the system\n");
    }
    choose_start_monitor();
    refresh_display();
    ui_ = std::make_unique<ui::Ui>(*this, *fonts_, keys_);
    init_audio();
    init_midi();
    init_song();
    return true;
}

void App::choose_start_monitor() {
    // Prefer a connected monitor whose saved profile asks to be taken over (e.g. the 4:3 PVM clone).
    const auto list = window_.monitors();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (profiles_.has(list[i].name)) {
            const auto p = profiles_.for_monitor(list[i].name);
            if (p.auto_apply && p.fullscreen) {
                window_.place(static_cast<int>(i), true);
                return;
            }
        }
    }
    const int cur = window_.current_monitor();
    if (cur >= 0 && profiles_.for_monitor(list[static_cast<std::size_t>(cur)].name).fullscreen) {
        window_.place(cur, true);
    }
}

void App::refresh_display() {
    const auto list = window_.monitors();
    const int idx = window_.current_monitor();
    display_.headless = false;
    display_.monitor_count = static_cast<int>(list.size());
    display_.monitor_index = std::max(idx, 0);
    if (idx >= 0) {
        const auto& m = list[static_cast<std::size_t>(idx)];
        if (m.name != display_.monitor_name || profile_.monitor != m.name) {
            display_.monitor_name = m.name;
            profile_ = profiles_.for_monitor(m.name);
        }
        display_.mode_w = m.mode_w;
        display_.mode_h = m.mode_h;
        display_.refresh_hz = m.refresh_hz;
    } else if (display_.monitor_name != "NO MONITOR") {
        // The OS reports no active display (e.g. screen off or a remote session): run windowed on
        // the default profile.
        display_.monitor_name = "NO MONITOR";
        profile_ = profiles_.fallback();
    }
    display_.fullscreen = window_.is_fullscreen();
    window_.framebuffer_size(display_.fb_w, display_.fb_h);
    display_.pixel_aspect = gfx::pixel_aspect_from_panel(profile_.panel_aspect, display_.mode_w, display_.mode_h);
    gfx::ViewportParams vp;
    vp.fb_w = display_.fb_w;
    vp.fb_h = display_.fb_h;
    vp.canvas_w = ui::kCanvasW;
    vp.canvas_h = ui::kCanvasH;
    vp.mode = profile_.scale;
    vp.pixel_aspect = display_.pixel_aspect;
    vp.overscan = profile_.overscan;
    const gfx::ScreenLayout layout = gfx::compute_screen_layout(vp, profile_.side_keys);
    display_.viewport = layout.viewport;
    side_w_ = layout.side_w;
    if (screen_.width() != ui::kCanvasW + 2 * side_w_) {
        screen_ = gfx::Canvas(ui::kCanvasW + 2 * side_w_, ui::kCanvasH);
    }
    if (ui_) {
        ui_->set_side_width(side_w_);
    }
}

void App::handle_monitor_changes() {
    if (!window_.take_monitors_changed()) {
        return;
    }
    const auto list = window_.monitors();
    std::vector<std::string> names;
    for (const auto& m : list) {
        names.push_back(m.name);
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        const bool is_new = std::find(known_monitors_.begin(), known_monitors_.end(), list[i].name) ==
                            known_monitors_.end();
        if (!is_new) {
            continue;
        }
        if (profiles_.has(list[i].name) && profiles_.for_monitor(list[i].name).auto_apply) {
            window_.place(static_cast<int>(i), profiles_.for_monitor(list[i].name).fullscreen);
            ui_->message("MONITOR " + to_upper(list[i].name) + " CONNECTED: PROFILE APPLIED");
        } else {
            ui_->message("MONITOR " + to_upper(list[i].name) + " CONNECTED");
        }
    }
    for (const auto& old : known_monitors_) {
        if (std::find(names.begin(), names.end(), old) == names.end()) {
            ui_->message("MONITOR " + to_upper(old) + " DISCONNECTED");
            if (old == display_.monitor_name && window_.is_fullscreen()) {
                window_.place(0, profiles_.for_monitor(list.empty() ? "" : list[0].name).fullscreen);
            }
        }
    }
    known_monitors_ = names;
}

void App::set_profile(const ui::DisplayProfile& profile) {
    const bool fullscreen_changed = profile.fullscreen != profile_.fullscreen;
    profile_ = profile;
    // Without a reported monitor, changes go to the default profile.
    profile_.monitor = display_.monitor_count > 0 ? display_.monitor_name : std::string{};
    profiles_.store(profile_);
    std::string error;
    if (!profiles_.save(profiles_path_, &error)) {
        ui_->message("ERROR: " + error, true);
    }
    if (fullscreen_changed) {
        window_.place(display_.monitor_index, profile_.fullscreen);
    }
}

std::filesystem::path App::snap_path(const std::string& requested, const char* prefix) const {
    if (!requested.empty()) {
        return requested;
    }
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char name[64];
    std::strftime(name, sizeof name, "-%Y%m%d-%H%M%S.png", &tm);
    return config_dir_ / "snaps" / (std::string(prefix) + name);
}

std::string App::display_command(const ui::Command& command) {
    if (command.arg_is(0, "NEXT")) {
        const auto list = window_.monitors();
        if (list.size() < 2) {
            return "DISPLAY: ONLY ONE MONITOR CONNECTED";
        }
        const int next = (display_.monitor_index + 1) % static_cast<int>(list.size());
        const auto& m = list[static_cast<std::size_t>(next)];
        window_.place(next, profiles_.for_monitor(m.name).fullscreen);
        return "DISPLAY: " + to_upper(m.name);
    }
    ui::DisplayProfile p = profile_;
    if (command.arg_is(0, "FULL")) {
        p.fullscreen = true;
    } else if (command.arg_is(0, "WINDOW")) {
        p.fullscreen = false;
    } else if (command.arg_is(0, "TOGGLE")) {
        p.fullscreen = !p.fullscreen;
    } else if (command.arg_is(0, "ASPECT")) {
        // DISPLAY ASPECT 16:9 (or 1.78): reshape the window, e.g. to try the side keys.
        double aspect = 0.0;
        if (command.args.size() > 1) {
            const std::string& a = command.args[1];
            const auto colon = a.find(':');
            aspect = colon == std::string::npos ? std::atof(a.c_str())
                                                : std::atof(a.substr(0, colon).c_str()) /
                                                      std::max(1e-9, std::atof(a.substr(colon + 1).c_str()));
        }
        if (aspect < 0.5 || aspect > 4.0) {
            return "ERROR: DISPLAY ASPECT W:H (E.G. 16:9, 16:10, 4:3)";
        }
        if (!window_.set_aspect(aspect)) {
            return "ERROR: DISPLAY ASPECT NEEDS A WINDOW (DISPLAY WINDOW)";
        }
        return "DISPLAY: WINDOW ASPECT " + to_upper(command.args[1]);
    } else {
        return "ERROR: DISPLAY NEXT|FULL|WINDOW|TOGGLE|ASPECT W:H";
    }
    set_profile(p);
    return p.fullscreen ? "DISPLAY: FULLSCREEN" : "DISPLAY: WINDOW";
}

std::optional<std::string> App::run_host_command(const ui::Command& command) {
    if (command.verb == "DISPLAY") {
        return display_command(command);
    }
    if (command.verb == "SNAP") {
        const auto path = snap_path(command.joined_args(), "snap");
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (!gfx::write_png(path, gfx::canvas_to_rgb(canvas_, ui_->theme()))) {
            return "ERROR: CANNOT WRITE " + path.filename().string();
        }
        std::fprintf(stdout, "snap: %s\n", path.string().c_str());
        return std::string{}; // scripting commands leave the INFO line as it is (it is in the picture)
    }
    if (command.verb == "SHOT") {
        const auto path = snap_path(command.joined_args(), "shot");
        pending_shot_ = path.string();
        return std::string{};
    }
    if (command.verb == "WAIT") {
        try {
            wait_frames_ = command.args.empty() ? 30 : std::max(0, std::stoi(command.args[0]));
        } catch (...) {
            return "ERROR: WAIT <FRAMES>";
        }
        return std::string{};
    }
    if (auto reply = audio_command(command)) {
        return reply;
    }
    if (auto reply = midi_command(command)) {
        return reply;
    }
    if (auto reply = song_command(command)) {
        return reply;
    }
    if (auto reply = loop_command(command)) {
        return reply;
    }
    if (auto reply = files_command(command)) {
        return reply;
    }
    if (command.verb == "QUIT") {
        window_.request_close();
        return std::string("BYE");
    }
    return std::nullopt;
}

void App::run_exec_queue() {
    if (frame_ < 3 || exec_.empty()) {
        return; // let the first frames settle (window placement, persistence buffers)
    }
    if (wait_frames_ > 0) {
        --wait_frames_;
        return;
    }
    const std::string cmd = exec_.front();
    exec_.pop_front();
    ui_->execute(cmd);
    std::fprintf(stdout, "exec: %s -> %s\n", cmd.c_str(), ui_->message_text().c_str());
}

int App::run() {
    if (!init()) {
        return 1;
    }
    ui::InputState input;
    double last = window_.time_s();
    while (!window_.should_close()) {
        input.begin_frame();
        window_.poll(input, display_.viewport, screen_.width(), ui::kCanvasH,
                     profile_.crt.enabled ? profile_.crt.curvature : 0.0f);
        input.pen_x -= static_cast<float>(side_w_); // canvas coordinates; the strips lie outside 0..kCanvasW
        const double now = window_.time_s();
        const double dt = now - last;
        last = now;
        time_s_ = now;

        handle_monitor_changes();
        refresh_display();
        update_audio();
        update_midi();
        update_samples();
        update_song();
        update_loops();
        update_files();
        ui_->update(input);
        run_exec_queue();
        refresh_display(); // commands may have moved the window or changed the profile

        ui_->draw(canvas_);
        screen_.clear(gfx::kInkBg);
        screen_.copy_from(canvas_, side_w_, 0);
        ui_->draw_side_keys(screen_);
        ui_->draw_pen(screen_, side_w_);
        crt_.render(screen_, ui_->theme(), profile_.crt, display_.viewport, display_.fb_w, display_.fb_h, dt);

        if (!pending_shot_.empty()) {
            const std::filesystem::path path = pending_shot_;
            pending_shot_.clear();
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            const auto img = crt_.read_framebuffer(display_.fb_w, display_.fb_h);
            if (!img || !gfx::write_png(path, *img)) {
                ui_->message("ERROR: SHOT FAILED", true);
            } else {
                std::fprintf(stdout, "shot: %s\n", path.string().c_str());
            }
        }
        window_.swap();
        ++frame_;
    }
    shutdown_audio();
    return 0;
}

} // namespace phos::app
