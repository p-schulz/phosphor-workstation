#include "ui/display_profile.h"

#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace phos::ui {

namespace {

using nlohmann::json;

constexpr std::array<PanelAspectChoice, 5> kPanelAspects = {{
    {"AUTO", 0.0},
    {"4:3", 4.0 / 3.0},
    {"5:4", 5.0 / 4.0},
    {"16:10", 16.0 / 10.0},
    {"16:9", 16.0 / 9.0},
}};

// Floats are written with 3 decimals so the file stays readable (0.35, not 0.3499999940395355).
double r3(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }

json crt_to_json(const CrtParams& c) {
    return json{{"enabled", c.enabled},   {"scanlines", r3(c.scanlines)},     {"mask", r3(c.mask)},
                {"bloom", r3(c.bloom)},       {"persistence", r3(c.persistence)}, {"vignette", r3(c.vignette)},
                {"curvature", r3(c.curvature)}, {"brightness", r3(c.brightness)}};
}

CrtParams crt_from_json(const json& j) {
    CrtParams c;
    if (!j.is_object()) {
        return c;
    }
    c.enabled = j.value("enabled", c.enabled);
    c.scanlines = j.value("scanlines", c.scanlines);
    c.mask = j.value("mask", c.mask);
    c.bloom = j.value("bloom", c.bloom);
    c.persistence = j.value("persistence", c.persistence);
    c.vignette = j.value("vignette", c.vignette);
    c.curvature = j.value("curvature", c.curvature);
    c.brightness = j.value("brightness", c.brightness);
    return c;
}

json profile_to_json(const DisplayProfile& p) {
    json j;
    if (!p.monitor.empty()) {
        j["monitor"] = p.monitor;
    }
    j["fullscreen"] = p.fullscreen;
    j["autoApply"] = p.auto_apply;
    j["scale"] = std::string(gfx::scale_mode_name(p.scale));
    j["sideKeys"] = std::string(gfx::side_keys_name(p.side_keys));
    j["panelAspect"] = panel_aspect_label(p.panel_aspect);
    j["overscan"] = json{{"left", r3(p.overscan.left)},
                         {"right", r3(p.overscan.right)},
                         {"top", r3(p.overscan.top)},
                         {"bottom", r3(p.overscan.bottom)}};
    j["theme"] = p.theme;
    j["crt"] = crt_to_json(p.crt);
    return j;
}

double parse_panel_aspect(const json& v) {
    if (v.is_number()) {
        return v.get<double>();
    }
    if (!v.is_string()) {
        return 0.0;
    }
    const auto s = v.get<std::string>();
    for (const auto& c : kPanelAspects) {
        if (s == c.label) {
            return c.value;
        }
    }
    const auto colon = s.find(':');
    if (colon == std::string::npos) {
        return 0.0;
    }
    try {
        const double a = std::stod(s.substr(0, colon));
        const double b = std::stod(s.substr(colon + 1));
        return b > 0 ? a / b : 0.0;
    } catch (...) {
        return 0.0;
    }
}

DisplayProfile profile_from_json(const json& j) {
    DisplayProfile p;
    if (!j.is_object()) {
        return p;
    }
    p.monitor = j.value("monitor", std::string{});
    p.fullscreen = j.value("fullscreen", p.fullscreen);
    p.auto_apply = j.value("autoApply", p.auto_apply);
    p.scale = gfx::parse_scale_mode(j.value("scale", std::string("FIT"))).value_or(gfx::ScaleMode::Fit);
    p.side_keys = gfx::parse_side_keys(j.value("sideKeys", std::string("AUTO"))).value_or(gfx::SideKeys::Auto);
    if (j.contains("panelAspect")) {
        p.panel_aspect = parse_panel_aspect(j["panelAspect"]);
    }
    if (j.contains("overscan") && j["overscan"].is_object()) {
        const json& o = j["overscan"];
        p.overscan.left = o.value("left", 0.0f);
        p.overscan.right = o.value("right", 0.0f);
        p.overscan.top = o.value("top", 0.0f);
        p.overscan.bottom = o.value("bottom", 0.0f);
    }
    p.theme = j.value("theme", p.theme);
    if (j.contains("crt")) {
        p.crt = crt_from_json(j["crt"]);
    }
    return p;
}

bool set_error(std::string* error, std::string msg) {
    if (error) {
        *error = std::move(msg);
    }
    return false;
}

} // namespace

std::span<const PanelAspectChoice> panel_aspect_choices() { return kPanelAspects; }

std::string panel_aspect_label(double value) {
    for (const auto& c : kPanelAspects) {
        if (std::abs(c.value - value) < 1e-6) {
            return std::string(c.label);
        }
    }
    return std::to_string(value);
}

DisplayProfile DisplayProfiles::for_monitor(std::string_view name) const {
    for (const auto& p : monitors_) {
        if (p.monitor == name) {
            return p;
        }
    }
    DisplayProfile p = fallback_;
    p.monitor = std::string(name);
    return p;
}

bool DisplayProfiles::has(std::string_view name) const {
    for (const auto& p : monitors_) {
        if (p.monitor == name) {
            return true;
        }
    }
    return false;
}

void DisplayProfiles::store(const DisplayProfile& profile) {
    if (profile.monitor.empty()) {
        fallback_ = profile;
        return;
    }
    for (auto& p : monitors_) {
        if (p.monitor == profile.monitor) {
            p = profile;
            return;
        }
    }
    monitors_.push_back(profile);
}

std::string DisplayProfiles::to_json() const {
    json j;
    j["schemaVersion"] = kSchemaVersion;
    json def = profile_to_json(fallback_);
    def.erase("monitor");
    j["default"] = def;
    json list = json::array();
    for (const auto& p : monitors_) {
        list.push_back(profile_to_json(p));
    }
    j["monitors"] = list;
    return j.dump(2) + "\n";
}

std::optional<DisplayProfiles> DisplayProfiles::from_json(std::string_view text, std::string* error) {
    const json j = json::parse(text, nullptr, false, true);
    if (j.is_discarded() || !j.is_object()) {
        set_error(error, "display profiles: invalid JSON");
        return std::nullopt;
    }
    const int version = j.value("schemaVersion", 0);
    if (version != kSchemaVersion) {
        set_error(error, "display profiles: unsupported schemaVersion " + std::to_string(version));
        return std::nullopt;
    }
    DisplayProfiles out;
    if (j.contains("default")) {
        out.fallback_ = profile_from_json(j["default"]);
        out.fallback_.monitor.clear();
    }
    if (j.contains("monitors") && j["monitors"].is_array()) {
        for (const auto& m : j["monitors"]) {
            DisplayProfile p = profile_from_json(m);
            if (!p.monitor.empty()) {
                out.store(p);
            }
        }
    }
    return out;
}

std::optional<DisplayProfiles> DisplayProfiles::load(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_error(error, "cannot read " + path.string());
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str(), error);
}

bool DisplayProfiles::save(const std::filesystem::path& path, std::string* error) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return set_error(error, "cannot write " + tmp.string());
        }
        out << to_json();
        if (!out) {
            return set_error(error, "write failed: " + tmp.string());
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        return set_error(error, "cannot replace " + path.string() + ": " + ec.message());
    }
    return true;
}

} // namespace phos::ui
