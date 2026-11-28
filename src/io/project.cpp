#include "io/project.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "io/import.h"
#include "io/project_detail.h"
#include "io/wav.h"

namespace phos::io {


namespace detail {

bool write_text(const std::filesystem::path& path, const std::string& text, std::string* error) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(error, "cannot write " + tmp.string());
        }
        out << text;
        if (!out) {
            return fail(error, "write failed: " + tmp.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return ec ? fail(error, "cannot replace " + path.string()) : true;
}

} // namespace detail

namespace {

using detail::fail;
using detail::index_of;
using detail::sanitize;

using nlohmann::json;
namespace fs = std::filesystem;

} // namespace

std::string project_json(const model::Document& doc, const std::string& audio_dir) {
    json j;
    j["schemaVersion"] = kProjectSchemaVersion;
    j["app"] = "PHOSPHOR";
    j["currentInstrument"] = doc.current;
    json insts = json::array();
    for (int i = 0; i < model::kInstruments; ++i) {
        insts.push_back(detail::instrument_json(doc.instruments[static_cast<std::size_t>(i)], i, audio_dir));
    }
    j["instruments"] = insts;

    j["song"] = detail::song_to_json(doc.song, audio_dir);
    return j.dump(2) + "\n";
}

bool migrate_project(std::string& text, std::string* error) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return fail(error, "project.json is not valid JSON");
    }
    int v = j.value("schemaVersion", 0);
    if (v < 1 || v > kProjectSchemaVersion) {
        return fail(error, "unsupported project schemaVersion " + std::to_string(v));
    }
    if (v == 1) { // 1 -> 2: instrument kinds; every v1 instrument is a SAMPLER
        for (auto& inst : j["instruments"]) {
            inst["kind"] = "SAMPLER";
        }
        v = 2;
    }
    if (v == 2) { // 2 -> 3: GB / SNES kinds, song echo and STRICT flags; defaults cover v2 files
        v = 3;
    }
    if (v == 3) { // 3 -> 4: loops, track types, inserts, sends, returns, master FX, VAPOR. Older songs
        v = 4;    // keep NOTES tracks without effects; returns get the default PLATE / DELAY.
    }
    if (v == 4) { // 4 -> 5: tracks play INTERNAL on channel = track number, no program, no automation
        v = 5;
    }
    if (v == 5) { // 5 -> 6: effect type GATE added; v5 files cannot contain it, nothing to change
        v = 6;
    }
    j["schemaVersion"] = v;
    text = j.dump(2) + "\n";
    return true;
}

bool save_project(const fs::path& bundle, const model::Document& doc, std::string* error, const char* json_name) {
    std::error_code ec;
    // An autosave writes its audio under autosave/, so the files project.json uses stay as saved.
    const bool real = std::string(json_name) == kProjectFile;
    const std::string dir = real ? std::string{} : std::string(kAutosaveDir) + "/";
    fs::create_directories(bundle / (dir + "samples"), ec);
    if (ec) {
        return fail(error, "cannot create " + bundle.string());
    }
    std::set<std::string> written;
    for (int i = 0; i < model::kInstruments; ++i) {
        if (!detail::write_instrument_files(bundle, doc.instruments[static_cast<std::size_t>(i)], i, dir, written, error)) {
            return false;
        }
    }
    if (!detail::save_loop_samples(bundle, doc.song, dir, error)) {
        return false;
    }
    if (!detail::write_text(bundle / json_name, project_json(doc, dir), error)) {
        return false;
    }
    // Drop sample files nothing refers to any more (in the folder just written).
    for (const auto& e : fs::directory_iterator(bundle / (dir + "samples"), ec)) {
        if (e.path().extension() == ".wav" && !written.count(e.path().filename().string())) {
            fs::remove(e.path(), ec);
        }
    }
    if (real) { // the saved project now holds everything: the autosave and its audio go
        fs::remove(bundle / kAutosaveFile, ec);
        fs::remove_all(bundle / kAutosaveDir, ec);
    }
    return true;
}

std::optional<model::Document> load_project(const fs::path& bundle, std::uint32_t engine_rate, std::string* error,
                                            const char* json_name) {
    std::ifstream in(bundle / json_name, std::ios::binary);
    if (!in) {
        fail(error, "no " + std::string(json_name) + " in " + bundle.filename().string());
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (!migrate_project(text, error)) {
        return std::nullopt;
    }
    const json j = json::parse(text);
    model::Document doc;
    doc.current = std::clamp(j.value("currentInstrument", 0), 0, model::kInstruments - 1);
    const json& insts = j.value("instruments", json::array());
    for (std::size_t i = 0; i < insts.size() && i < static_cast<std::size_t>(model::kInstruments); ++i) {
        if (!detail::instrument_from_json(insts[i], doc.instruments[i], bundle, engine_rate, error)) {
            return std::nullopt;
        }
    }

    if (!detail::song_from_json(j.value("song", json::object()), doc.song, bundle, engine_rate, error)) {
        return std::nullopt;
    }
    return doc;
}

fs::path bundle_path(const fs::path& projects_folder, const std::string& name_or_path) {
    fs::path p = name_or_path;
    if (p.extension() == ".phosphor" && (p.is_absolute() || p.has_parent_path())) {
        return p;
    }
    if (p.extension() != ".phosphor") {
        p += ".phosphor";
    }
    return p.is_absolute() ? p : projects_folder / p;
}

bool is_project_bundle(const fs::path& path) {
    std::error_code ec;
    return path.extension() == ".phosphor" && fs::is_directory(path, ec);
}

} // namespace phos::io
