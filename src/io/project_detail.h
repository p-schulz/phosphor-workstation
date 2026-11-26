#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "model/instrument.h"
#include "model/song.h"

// Internals shared by io/project.cpp (instruments, bundle files) and io/project_song.cpp (song,
// loops, effects, VAPOR). Not part of the public io API.
namespace phos::io::detail {

bool fail(std::string* error, std::string text);
// Letters, digits and '-' kept, the rest '_', at most 32 characters.
std::string sanitize(const std::string& name);

// Index of `j[key]` in `names`, or `fallback`.
template <std::size_t N>
int index_of(const std::array<const char*, N>& names, const nlohmann::json& j, const char* key, int fallback) {
    const std::string v = j.value(key, std::string{});
    for (std::size_t i = 0; i < N; ++i) {
        if (v == names[i]) {
            return static_cast<int>(i);
        }
    }
    return fallback;
}

// Writes `s` as a 32-bit float WAV at `bundle / rel` (via a temporary file).
bool write_sample(const std::filesystem::path& bundle, const std::string& rel, const model::SampleData& s,
                  std::string* error);
// Reads `bundle / rel` and resamples it to `engine_rate`. `ratio` (optional) gets engine / file rate.
model::SampleRef read_sample(const std::filesystem::path& bundle, const std::string& rel, std::uint32_t engine_rate,
                             const std::string& name, const std::string& source, std::string* error,
                             double* ratio = nullptr);

// Writes `text` to `path` via a temporary file.
bool write_text(const std::filesystem::path& path, const std::string& text, std::string* error);

// One instrument (io/instrument_io.cpp). `index` names its files (samples/NN_<name>.wav,
// brr/NN.brr, wavetables/NN.wav) under `audio_dir`; `written` collects the sample file names.
nlohmann::json instrument_json(const model::Instrument& inst, int index, const std::string& audio_dir);
bool write_instrument_files(const std::filesystem::path& bundle, const model::Instrument& inst, int index,
                            const std::string& audio_dir, std::set<std::string>& written, std::string* error);
bool instrument_from_json(const nlohmann::json& o, model::Instrument& inst, const std::filesystem::path& bundle,
                          std::uint32_t engine_rate, std::string* error);

nlohmann::json song_to_json(const model::Song& s, const std::string& audio_dir);
// Fills `s` from `song_json`; loop samples are read from `bundle` and resampled to `engine_rate`.
bool song_from_json(const nlohmann::json& song_json, model::Song& s, const std::filesystem::path& bundle,
                    std::uint32_t engine_rate, std::string* error);
// Writes each loop slot's sample to <audio_dir>loops/NN_<name>.wav and removes stale loop files there.
bool save_loop_samples(const std::filesystem::path& bundle, const model::Song& s, const std::string& audio_dir,
                       std::string* error);
std::string loop_file(int slot, const model::LoopSlot& loop, const std::string& audio_dir);

} // namespace phos::io::detail
