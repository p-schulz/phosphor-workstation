#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "model/instrument.h"
#include "model/sample_data.h"

namespace phos::io {

// Decoded audio at the file's own rate, planar, at most two channels (more are mixed down).
struct DecodedAudio {
    std::uint32_t sample_rate = 0;
    int source_channels = 0;
    std::vector<float> left;
    std::vector<float> right; // empty = mono
    std::string format;       // "WAV", "AIFF", "FLAC", "MP3", "OGG"
};

// WAV `smpl` chunk (RIFF "smpl": MIDI unity note at +12, loops from +36, 24 bytes each).
struct SmplInfo {
    int unity_note = -1;
    bool has_loop = false;
    std::size_t loop_start = 0; // frames
    std::size_t loop_end = 0;   // exclusive (smpl stores the last frame)
    bool ping_pong = false;     // loop type 1 = alternating
};

// Decodes WAV, AIFF/AIFC, FLAC, MP3 and Ogg Vorbis (miniaudio decoders; Vorbis via stb_vorbis).
std::optional<DecodedAudio> decode_file(const std::filesystem::path& path, std::string* error);
// Reads the smpl chunk of a WAV file, if any.
std::optional<SmplInfo> read_smpl(const std::filesystem::path& path);
// High-quality resampling (r8brain-free-src, 24-bit quality preset).
std::vector<float> resample(const std::vector<float>& in, double from_hz, double to_hz);

bool is_audio_file(const std::filesystem::path& path);

struct ImportOptions {
    std::uint32_t target_rate = 48000;
    bool normalize = false;
};

struct ImportResult {
    model::SampleRef sample;
    model::SamplerParams params; // root note and loop from the file, the rest default
    std::string root_source;     // "NAME", "SMPL", "PITCH" or "DEFAULT"
    std::string description;     // e.g. "WAV 44100 HZ 2 CH -> 48000 HZ"
    std::string error;
    bool ok() const { return static_cast<bool>(sample); }
};

// The import pipeline (PLAN.md §7.2): decode -> float32 -> resample to the engine rate ->
// optional normalise -> overview -> root key (file name, smpl unity note, YIN, else C4) and loop
// points (smpl). Runs on a worker thread; touches nothing shared.
ImportResult import_sample(const std::filesystem::path& path, const ImportOptions& options);

} // namespace phos::io
