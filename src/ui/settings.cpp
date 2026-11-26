#include "ui/settings.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/text.h"
#include "ui/input.h"

namespace phos::ui {

using nlohmann::json;

namespace {

bool set_error(std::string* error, std::string text) {
    if (error) {
        *error = std::move(text);
    }
    return false;
}

constexpr std::array<std::string_view, 2> kPlacements = {"IMMEDIATE", "STABLE"};
constexpr std::array<std::string_view, 4> kCurves = {"LINEAR", "SOFT", "HARD", "FIXED"};

template <std::size_t N> int index_of(const std::array<std::string_view, N>& names, const std::string& v) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == v) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

MidiInputSetting* MidiConfig::find(std::string_view name) {
    for (auto& i : inputs) {
        if (i.name == name) {
            return &i;
        }
    }
    return nullptr;
}

const MidiInputSetting* MidiConfig::find(std::string_view name) const {
    return const_cast<MidiConfig*>(this)->find(name);
}

const char* page_modifier_name(int mods) {
    return mods == kModCtrl ? "CTRL" : mods == kModAlt ? "ALT" : mods == kModSuper ? "CMD" : "OFF";
}

std::optional<int> page_modifier_from(std::string_view name) {
    const std::string n = to_upper(name);
    if (n == "OFF") {
        return 0;
    }
    if (n == "CTRL" || n == "CONTROL") {
        return kModCtrl;
    }
    if (n == "ALT" || n == "OPTION") {
        return kModAlt;
    }
    if (n == "CMD" || n == "SUPER" || n == "WIN") {
        return kModSuper;
    }
    return std::nullopt;
}

MidiOutputSetting* MidiOutConfig::find(std::string_view name) {
    for (auto& o : outputs) {
        if (o.name == name) {
            return &o;
        }
    }
    return nullptr;
}

const MidiOutputSetting* MidiOutConfig::find(std::string_view name) const {
    return const_cast<MidiOutConfig*>(this)->find(name);
}

std::string Settings::to_json() const {
    json j;
    json outs = json::array();
    for (const auto& o : midi_out.outputs) {
        outs.push_back({{"name", o.name}, {"enabled", o.enabled}, {"clock", o.clock}});
    }
    j["midiOut"] = {{"enabled", midi_out.enabled},
                    {"outputs", outs},
                    {"offsetMs", midi_out.offset_ms},
                    {"latencyComp", midi_out.latency_comp},
                    {"thru", midi_out.thru}};
    j["schemaVersion"] = kSchemaVersion;
    j["audio"] = {
        {"enabled", audio.enabled},
        {"device", audio.device},
        {"sampleRate", audio.sample_rate},
        {"bufferFrames", audio.buffer_frames},
        {"exclusive", audio.exclusive},
    };
    json inputs = json::array();
    for (const auto& i : midi.inputs) {
        inputs.push_back({{"name", i.name}, {"enabled", i.enabled}, {"channel", i.channel < 0 ? 0 : i.channel + 1}});
    }
    j["midi"] = {
        {"inputs", inputs},
        {"placement", kPlacements[static_cast<std::size_t>(midi.placement)]},
        {"velocityCurve", kCurves[static_cast<std::size_t>(midi.velocity_curve)]},
        {"bendRange", midi.bend_range},
        {"keyjazzOctave", midi.keyjazz_octave},
        {"keyjazzVelocity", midi.keyjazz_velocity},
        {"masterCc", midi.master_cc},
        {"masterCcChannel", midi.master_cc_channel < 0 ? 0 : midi.master_cc_channel + 1},
    };
    j["disk"] = {{"folder", browse_folder}, {"projects", projects_folder}};
    j["keys"] = {{"pageModifier", page_modifier_name(page_modifier)}};
    j["composer"] = {{"follow", follow}};
    return j.dump(2) + "\n";
}

std::optional<Settings> Settings::from_json(std::string_view text, std::string* error) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        set_error(error, "settings: not valid JSON");
        return std::nullopt;
    }
    const int version = j.value("schemaVersion", 0);
    if (version != kSchemaVersion) {
        set_error(error, "settings: unsupported schemaVersion " + std::to_string(version));
        return std::nullopt;
    }
    Settings s;
    if (const auto it = j.find("audio"); it != j.end() && it->is_object()) {
        const json& a = *it;
        s.audio.enabled = a.value("enabled", s.audio.enabled);
        s.audio.device = a.value("device", s.audio.device);
        s.audio.sample_rate = std::clamp(a.value("sampleRate", s.audio.sample_rate), 8000u, 192000u);
        s.audio.buffer_frames = std::clamp(a.value("bufferFrames", s.audio.buffer_frames), 32u, 1024u);
        s.audio.exclusive = a.value("exclusive", s.audio.exclusive);
    }
    if (const auto it = j.find("midiOut"); it != j.end() && it->is_object()) {
        MidiOutConfig& o = s.midi_out;
        o.enabled = it->value("enabled", false);
        o.offset_ms = std::clamp(it->value("offsetMs", 0), -100, 100);
        o.latency_comp = it->value("latencyComp", true);
        o.thru = it->value("thru", true);
        for (const auto& p : it->value("outputs", json::array())) {
            if (p.is_object() && !p.value("name", std::string{}).empty()) {
                o.outputs.push_back(MidiOutputSetting{p.value("name", std::string{}), p.value("enabled", false),
                                                      p.value("clock", false)});
            }
        }
    }
    if (const auto it = j.find("midi"); it != j.end() && it->is_object()) {
        const json& m = *it;
        MidiConfig& c = s.midi;
        if (const auto in = m.find("inputs"); in != m.end() && in->is_array()) {
            for (const auto& e : *in) {
                if (!e.is_object() || !e.contains("name") || !e["name"].is_string()) {
                    continue;
                }
                MidiInputSetting i;
                i.name = e["name"].get<std::string>();
                i.enabled = e.value("enabled", true);
                i.channel = std::clamp(e.value("channel", 0), 0, 16) - 1; // stored 0 = all, 1..16
                if (!c.find(i.name)) {
                    c.inputs.push_back(i);
                }
            }
        }
        if (const int p = index_of(kPlacements, m.value("placement", std::string{})); p >= 0) {
            c.placement = static_cast<engine::Placement>(p);
        }
        if (const int v = index_of(kCurves, m.value("velocityCurve", std::string{})); v >= 0) {
            c.velocity_curve = static_cast<engine::VelocityCurve>(v);
        }
        c.bend_range = std::clamp(m.value("bendRange", c.bend_range), 0, 24);
        c.keyjazz_octave = std::clamp(m.value("keyjazzOctave", c.keyjazz_octave), 0, 8);
        c.keyjazz_velocity = std::clamp(m.value("keyjazzVelocity", c.keyjazz_velocity), 1, 127);
        c.master_cc = std::clamp(m.value("masterCc", c.master_cc), -1, 127);
        c.master_cc_channel = std::clamp(m.value("masterCcChannel", 0), 0, 16) - 1;
    }
    if (const auto it = j.find("composer"); it != j.end() && it->is_object()) {
        s.follow = it->value("follow", true);
    }
    if (const auto it = j.find("keys"); it != j.end() && it->is_object()) {
        if (const auto m = page_modifier_from(it->value("pageModifier", std::string{}))) {
            s.page_modifier = *m;
        }
    }
    if (const auto it = j.find("disk"); it != j.end() && it->is_object()) {
        s.browse_folder = it->value("folder", std::string{});
        s.projects_folder = it->value("projects", std::string{});
    }
    return s;
}

std::optional<Settings> Settings::load(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_error(error, "cannot read " + path.string());
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str(), error);
}

bool Settings::save(const std::filesystem::path& path, std::string* error) const {
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
