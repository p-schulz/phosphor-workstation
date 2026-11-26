#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "model/workspace.h"

// RENDER / --render-audio (PLAN.md §7.9): the song through the offline engine into WAV files,
// 24-bit PCM or 32-bit float, optionally with one stem per sounding track.
namespace phos::io {

struct ExportOptions {
    bool stems = false;
    bool float32 = false; // else 24-bit PCM
    std::uint32_t sample_rate = 48000;
    double tail_s = 2.0;
};

struct ExportResult {
    bool ok = false;
    std::string error;
    std::vector<std::filesystem::path> files; // the mix first, then the stems
    double seconds = 0.0;                     // length of the render
};

// Writes `out` (the mix) and, with stems, `<out stem>_<track label>_<track name>.wav` next to it.
ExportResult export_audio(const model::Document& doc, const std::filesystem::path& out, const ExportOptions& opt);

} // namespace phos::io
