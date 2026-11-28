#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/viewport.h"

namespace phos::ui {

// CRT post-pass parameters, all 0..1 unless noted.
struct CrtParams {
    bool enabled = true;
    float scanlines = 0.45f;
    float mask = 0.15f;
    float bloom = 0.35f;
    float persistence = 0.40f; // phosphor decay; 0 = none
    float vignette = 0.25f;
    float curvature = 0.0f;
    float brightness = 0.0f; // -0.5..0.5
    friend bool operator==(const CrtParams&, const CrtParams&) = default;
};

struct DisplayProfile {
    std::string monitor; // GLFW monitor name this profile belongs to; empty = default profile
    bool fullscreen = false;
    bool auto_apply = true; // move here (and apply) when this monitor connects
    gfx::ScaleMode scale = gfx::ScaleMode::Fit;
    double panel_aspect = 0.0; // physical panel aspect override; 0 = square pixels (AUTO)
    gfx::Insets overscan;
    gfx::SideKeys side_keys = gfx::SideKeys::Auto; // touch / click strips beside a 4:3 canvas
    std::string theme = "GREEN";
    CrtParams crt;
    friend bool operator==(const DisplayProfile&, const DisplayProfile&) = default;
};

struct PanelAspectChoice {
    std::string_view label;
    double value;
};
std::span<const PanelAspectChoice> panel_aspect_choices();
std::string panel_aspect_label(double value);

class DisplayProfiles {
public:
    static constexpr int kSchemaVersion = 1;

    // Exact monitor-name match, else the default profile (with `monitor` set to the requested name).
    DisplayProfile for_monitor(std::string_view name) const;
    bool has(std::string_view name) const;
    // Inserts or replaces the profile for profile.monitor (empty name = default profile).
    void store(const DisplayProfile& profile);

    const DisplayProfile& fallback() const { return fallback_; }
    const std::vector<DisplayProfile>& monitors() const { return monitors_; }

    std::string to_json() const;
    static std::optional<DisplayProfiles> from_json(std::string_view text, std::string* error);

    static std::optional<DisplayProfiles> load(const std::filesystem::path& path, std::string* error);
    bool save(const std::filesystem::path& path, std::string* error) const;

private:
    DisplayProfile fallback_;
    std::vector<DisplayProfile> monitors_;
};

} // namespace phos::ui
