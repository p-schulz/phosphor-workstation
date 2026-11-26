// fixturegen: writes the WAV test fixtures in tests/fixtures/ (own synthesised content only).
// The non-WAV formats are converted from tone_44k_stereo.wav by tests/fixtures/make_fixtures.sh.
// Uses its own little WAV writer on purpose, so the importer is not tested against itself.
//
//   fixturegen <output dir>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

namespace {

enum class Fmt { U8, S16, S24, S32, F32 };

struct Loop {
    std::uint32_t start = 0;
    std::uint32_t end_inclusive = 0;
    std::uint32_t type = 0; // 0 forward, 1 alternating
};

void put(std::vector<unsigned char>& o, std::uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        o.push_back(static_cast<unsigned char>(v >> (8 * i)));
    }
}

void write_wav(const std::filesystem::path& path, const std::vector<float>& interleaved, int channels, std::uint32_t rate,
               Fmt fmt, int unity_note = -1, const Loop* loop = nullptr) {
    const int bits = fmt == Fmt::U8 ? 8 : fmt == Fmt::S16 ? 16 : fmt == Fmt::S24 ? 24 : 32;
    const int bps = bits / 8;
    std::vector<unsigned char> data;
    for (const float x : interleaved) {
        const double v = std::fmax(-1.0, std::fmin(1.0, x));
        switch (fmt) {
        case Fmt::U8:
            data.push_back(static_cast<unsigned char>(std::lround(v * 127.0) + 128));
            break;
        case Fmt::S16:
            put(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * 32767.0))), 2);
            break;
        case Fmt::S24:
            put(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * 8388607.0))), 3);
            break;
        case Fmt::S32:
            put(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::llround(v * 2147483647.0))), 4);
            break;
        case Fmt::F32: {
            const float f = static_cast<float>(v);
            std::uint32_t u;
            static_assert(sizeof u == sizeof f);
            std::memcpy(&u, &f, 4);
            put(data, u, 4);
            break;
        }
        }
    }
    std::vector<unsigned char> smpl;
    if (unity_note >= 0 || loop) {
        put(smpl, 0, 4);                                   // manufacturer
        put(smpl, 0, 4);                                   // product
        put(smpl, 1000000000u / rate, 4);                  // sample period (ns)
        put(smpl, static_cast<std::uint32_t>(unity_note < 0 ? 60 : unity_note), 4);
        put(smpl, 0, 4);                                   // pitch fraction
        put(smpl, 0, 4);                                   // SMPTE format
        put(smpl, 0, 4);                                   // SMPTE offset
        put(smpl, loop ? 1u : 0u, 4);                      // loop count
        put(smpl, 0, 4);                                   // sampler data
        if (loop) {
            put(smpl, 0, 4);                               // cue point id
            put(smpl, loop->type, 4);
            put(smpl, loop->start, 4);
            put(smpl, loop->end_inclusive, 4);
            put(smpl, 0, 4);                               // fraction
            put(smpl, 0, 4);                               // play count (infinite)
        }
    }
    std::vector<unsigned char> out = {'R', 'I', 'F', 'F'};
    const std::uint32_t fmt_size = 16;
    const std::uint32_t riff = 4 + 8 + fmt_size + 8 + static_cast<std::uint32_t>(data.size()) +
                               (smpl.empty() ? 0 : 8 + static_cast<std::uint32_t>(smpl.size()));
    put(out, riff, 4);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(out, fmt_size, 4);
    put(out, fmt == Fmt::F32 ? 3u : 1u, 2);
    put(out, static_cast<std::uint32_t>(channels), 2);
    put(out, rate, 4);
    put(out, rate * static_cast<std::uint32_t>(channels * bps), 4);
    put(out, static_cast<std::uint32_t>(channels * bps), 2);
    put(out, static_cast<std::uint32_t>(bits), 2);
    if (!smpl.empty()) { // before "data", as many samplers write it
        out.insert(out.end(), {'s', 'm', 'p', 'l'});
        put(out, static_cast<std::uint32_t>(smpl.size()), 4);
        out.insert(out.end(), smpl.begin(), smpl.end());
    }
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put(out, static_cast<std::uint32_t>(data.size()), 4);
    out.insert(out.end(), data.begin(), data.end());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    std::printf("wrote %s\n", path.string().c_str());
}

// A few harmonics with a soft decay: pitched, so YIN finds it, and not a pure sine.
std::vector<float> tone(double hz, double seconds, std::uint32_t rate, int channels, double decay = 3.0) {
    const auto n = static_cast<std::size_t>(seconds * rate);
    std::vector<float> out(n * static_cast<std::size_t>(channels));
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double env = std::exp(-decay * t) * std::fmin(1.0, t * 200.0);
        double v = 0.0;
        for (int h = 1; h <= 4; ++h) {
            v += std::sin(2.0 * std::numbers::pi * hz * h * t) / h;
        }
        v *= 0.5 * env;
        for (int c = 0; c < channels; ++c) {
            out[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] =
                static_cast<float>(c == 1 ? v * 0.8 : v); // right slightly quieter: channels differ
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: fixturegen <output dir>\n", stderr);
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);

    // Format matrix (A3 = 220 Hz, root note in the name).
    const auto mono = tone(220.0, 0.25, 44100, 1);
    write_wav(dir / "pcm8_A3.wav", mono, 1, 44100, Fmt::U8);
    write_wav(dir / "pcm16_A3.wav", mono, 1, 44100, Fmt::S16);
    write_wav(dir / "pcm24_A3.wav", mono, 1, 44100, Fmt::S24);
    write_wav(dir / "pcm32_A3.wav", mono, 1, 44100, Fmt::S32);
    write_wav(dir / "float32_A3.wav", mono, 1, 44100, Fmt::F32);
    // Source for the AIFF / FLAC / MP3 / OGG conversions.
    write_wav(dir / "tone_44k_stereo.wav", tone(220.0, 0.5, 44100, 2), 2, 44100, Fmt::S16);

    // smpl loops: E4 (330 Hz) sustained tone, unity note 64, loop 12000..35999.
    const auto sustained = tone(329.6276, 1.0, 48000, 1, 0.2);
    const Loop fwd{12000, 35999, 0};
    const Loop alt{12000, 35999, 1};
    write_wav(dir / "loop_forward.wav", sustained, 1, 48000, Fmt::S16, 64, &fwd);
    write_wav(dir / "loop_pingpong.wav", sustained, 1, 48000, Fmt::S16, 64, &alt);

    // No note in the name and no smpl chunk: root key from pitch detection (G3 = 196 Hz).
    write_wav(dir / "pitched_tone.wav", tone(195.9977, 0.5, 48000, 1, 1.0), 1, 48000, Fmt::S16);
    return 0;
}
