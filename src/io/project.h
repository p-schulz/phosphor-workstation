#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "model/workspace.h"

// Project bundles (PLAN.md §7.9): `Name.phosphor/` holds project.json (schemaVersion, migrations)
// and samples/ with each instrument's current audio as 32-bit float WAV (edits are destructive, so
// the bundle keeps the edited audio; the original file path is recorded for reference).
namespace phos::io {

// 2: instrument kinds (M5); 3: GB, SNES, echo, STRICT (M6); 4: loops, FX, sends, VAPOR (M7);
// 5: per-track MIDI destination, CC automation lanes (M8)
inline constexpr int kProjectSchemaVersion = 6;
inline constexpr const char* kProjectFile = "project.json";
inline constexpr const char* kAutosaveFile = "project.autosave.json";
// The autosave's own copy of the audio (samples/, loops/, brr/, wavetables/ inside it), so an
// autosave never changes the files project.json refers to. A real save removes it.
inline constexpr const char* kAutosaveDir = "autosave";

// Deterministic JSON for a document (sorted keys, fixed order): save -> load -> save is identical.
// `audio_dir`: prefix of the audio paths ("" for project.json, "autosave/" for the autosave).
std::string project_json(const model::Document& doc, const std::string& audio_dir = {});

// Writes the samples and `json_name` into `bundle` (created if needed). Atomic per file.
bool save_project(const std::filesystem::path& bundle, const model::Document& doc, std::string* error,
                  const char* json_name = kProjectFile);
// Reads `json_name` from `bundle`; samples are resampled to `engine_rate` if they differ.
std::optional<model::Document> load_project(const std::filesystem::path& bundle, std::uint32_t engine_rate,
                                            std::string* error, const char* json_name = kProjectFile);

// Brings older project JSON up to kProjectSchemaVersion (none needed yet; hook for M5+).
bool migrate_project(std::string& json_text, std::string* error);

// "My Song" -> "<folder>/My Song.phosphor"; a path ending in .phosphor is used as it is.
std::filesystem::path bundle_path(const std::filesystem::path& projects_folder, const std::string& name_or_path);
bool is_project_bundle(const std::filesystem::path& path);

} // namespace phos::io
