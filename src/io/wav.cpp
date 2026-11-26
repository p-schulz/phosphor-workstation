#include "io/wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace phos::io {

namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }

void put32(std::vector<unsigned char>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<unsigned char>(v >> (8 * i)));
    }
}
void put16(std::vector<unsigned char>& out, std::uint16_t v) {
    out.push_back(static_cast<unsigned char>(v));
    out.push_back(static_cast<unsigned char>(v >> 8));
}

bool fail(std::string* error, const char* msg) {
    if (error) {
        *error = msg;
    }
    return false;
}

} // namespace

std::optional<AudioBuffer> parse_wav(const std::vector<unsigned char>& b, std::string* error) {
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        fail(error, "not a RIFF/WAVE file");
        return std::nullopt;
    }
    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t bits = 0;
    const unsigned char* data = nullptr;
    std::size_t data_size = 0;

    std::size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const unsigned char* id = b.data() + pos;
        const std::size_t size = le32(b.data() + pos + 4);
        const std::size_t body = pos + 8;
        const std::size_t avail = std::min(size, b.size() - body);
        if (std::memcmp(id, "fmt ", 4) == 0 && avail >= 16) {
            format = le16(b.data() + body);
            channels = le16(b.data() + body + 2);
            rate = le32(b.data() + body + 4);
            bits = le16(b.data() + body + 14);
            if (format == kFormatExtensible && avail >= 26) {
                format = le16(b.data() + body + 24); // first two bytes of the SubFormat GUID
            }
        } else if (std::memcmp(id, "data", 4) == 0) {
            data = b.data() + body;
            data_size = avail; // tolerate truncated files and size 0xFFFFFFFF from streaming writers
        }
        pos = body + size + (size & 1); // chunks are word aligned
    }
    if (channels == 0 || rate == 0 || !data) {
        fail(error, "missing fmt or data chunk");
        return std::nullopt;
    }
    const bool is_float = format == kFormatFloat && (bits == 32 || bits == 64);
    const bool is_pcm = format == kFormatPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    if (!is_float && !is_pcm) {
        fail(error, "unsupported WAV sample format");
        return std::nullopt;
    }
    const std::size_t bytes_per = bits / 8u;
    const std::size_t count = data_size / bytes_per / channels * channels;

    AudioBuffer out;
    out.sample_rate = rate;
    out.channels = channels;
    out.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char* p = data + i * bytes_per;
        float v = 0.0f;
        if (is_float && bits == 32) {
            std::memcpy(&v, p, 4);
        } else if (is_float) {
            double d;
            std::memcpy(&d, p, 8);
            v = static_cast<float>(d);
        } else if (bits == 8) {
            v = (static_cast<float>(p[0]) - 128.0f) / 128.0f; // 8-bit WAV is unsigned
        } else if (bits == 16) {
            v = static_cast<float>(static_cast<std::int16_t>(le16(p))) / 32768.0f;
        } else if (bits == 24) {
            const auto u = static_cast<std::uint32_t>(p[0] | p[1] << 8 | p[2] << 16);
            const auto s = static_cast<std::int32_t>(u << 8) >> 8;
            v = static_cast<float>(s) / 8388608.0f;
        } else {
            v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(le32(p))) / 2147483648.0);
        }
        out.samples[i] = v;
    }
    return out;
}

std::optional<AudioBuffer> read_wav(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(error, "cannot open file");
        return std::nullopt;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse_wav(bytes, error);
}

std::vector<unsigned char> encode_wav(const AudioBuffer& buf, WavFormat format) {
    const std::uint16_t bits = format == WavFormat::Pcm16 ? 16 : format == WavFormat::Pcm24 ? 24 : 32;
    const std::uint32_t bytes_per = bits / 8u;
    const auto data_size = static_cast<std::uint32_t>(buf.samples.size() * bytes_per);
    std::vector<unsigned char> out;
    out.reserve(44 + data_size);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(out, 36 + data_size);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(out, 16);
    put16(out, format == WavFormat::Float32 ? kFormatFloat : kFormatPcm);
    put16(out, static_cast<std::uint16_t>(buf.channels));
    put32(out, buf.sample_rate);
    put32(out, buf.sample_rate * buf.channels * bytes_per);
    put16(out, static_cast<std::uint16_t>(buf.channels * bytes_per));
    put16(out, bits);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(out, data_size);
    for (const float s : buf.samples) {
        if (format == WavFormat::Float32) {
            std::uint32_t u;
            std::memcpy(&u, &s, 4);
            put32(out, u);
            continue;
        }
        const double full = format == WavFormat::Pcm16 ? 32767.0 : 8388607.0;
        const auto v = static_cast<std::int32_t>(std::lround(std::clamp(static_cast<double>(s), -1.0, 1.0) * full));
        const auto u = static_cast<std::uint32_t>(v);
        out.push_back(static_cast<unsigned char>(u));
        out.push_back(static_cast<unsigned char>(u >> 8));
        if (format == WavFormat::Pcm24) {
            out.push_back(static_cast<unsigned char>(u >> 16));
        }
    }
    return out;
}

bool write_wav(const std::filesystem::path& path, const AudioBuffer& buffer, WavFormat format, std::string* error) {
    const auto bytes = encode_wav(buffer, format);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return fail(error, "cannot create file");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out ? true : fail(error, "write failed");
}

} // namespace phos::io
