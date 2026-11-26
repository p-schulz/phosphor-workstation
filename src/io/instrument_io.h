#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "model/instrument.h"

// The instrument library (owner request, after M8): one instrument as a bundle `NAME.phinst/` with
// instrument.json (the same JSON as inside a project, same schemaVersion and migrations) and its
// audio (samples/, wavetables/, brr/). Copy them between machines or projects; IMPORT loads one
// into the current instrument slot.
namespace phos::io {

inline constexpr const char* kInstrumentFile = "instrument.json";
inline constexpr const char* kInstrumentExt = ".phinst";

bool save_instrument(const std::filesystem::path& bundle, const model::Instrument& inst, std::string* error);
// Audio is resampled to `engine_rate`. DRUM pads that play another instrument keep their slot
// number: in a new project they play whatever sits in that slot.
std::optional<model::Instrument> load_instrument(const std::filesystem::path& bundle, std::uint32_t engine_rate,
                                                 std::string* error);

// "Fat Bass" -> "<library>/Fat Bass.phinst"; a path ending in .phinst is used as it is.
std::filesystem::path instrument_path(const std::filesystem::path& library, const std::string& name_or_path);
bool is_instrument_bundle(const std::filesystem::path& path);
// The instrument bundles in `folder` (not recursive), sorted by name.
std::vector<std::filesystem::path> list_instruments(const std::filesystem::path& folder);

} // namespace phos::io
