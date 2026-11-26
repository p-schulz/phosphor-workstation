#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace phos::io {

struct AudioBuffer {
    unsigned sample_rate = 48000;
    unsigned channels = 2;
    std::vector<float> samples; // interleaved, frames * channels
    std::size_t frames() const { return channels ? samples.size() / channels : 0; }
};

enum class WavFormat { Pcm16, Pcm24, Float32 };

// RIFF/WAVE reader: PCM 8/16/24/32-bit integer and 32/64-bit float, plain or WAVE_FORMAT_EXTENSIBLE.
// Extra chunks are skipped. Samples are converted to float in [-1, 1).
std::optional<AudioBuffer> read_wav(const std::filesystem::path& path, std::string* error);
std::optional<AudioBuffer> parse_wav(const std::vector<unsigned char>& bytes, std::string* error);

// Integer formats are written with rounding and clamping (no dither).
bool write_wav(const std::filesystem::path& path, const AudioBuffer& buffer, WavFormat format, std::string* error);
std::vector<unsigned char> encode_wav(const AudioBuffer& buffer, WavFormat format);

} // namespace phos::io
