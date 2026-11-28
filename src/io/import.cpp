#include "io/import.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#include <CDSPResampler.h>
#include <miniaudio.h>

#include "core/notes.h"
#include "core/text.h"
#include "dsp/yin.h"

namespace phos::io {

namespace {

constexpr std::array<std::string_view, 8> kExtensions = {".wav", ".wave", ".aif", ".aiff", ".aifc", ".flac", ".mp3", ".ogg"};

std::string format_of(const std::filesystem::path& p) {
    std::string ext = to_upper(p.extension().string());
    if (ext == ".WAVE") {
        return "WAV";
    }
    if (ext == ".AIF" || ext == ".AIFC") {
        return "AIFF";
    }
    return ext.size() > 1 ? ext.substr(1) : "AUDIO";
}

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

} // namespace

bool is_audio_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

std::optional<DecodedAudio> decode_file(const std::filesystem::path& path, std::string* error) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); // native channels and rate
    ma_decoder dec;
#if defined(_WIN32)
    const ma_result r = ma_decoder_init_file_w(path.wstring().c_str(), &cfg, &dec);
#else
    const ma_result r = ma_decoder_init_file(path.string().c_str(), &cfg, &dec);
#endif
    if (r != MA_SUCCESS) {
        if (error) {
            *error = "unsupported or unreadable file";
        }
        return std::nullopt;
    }
    DecodedAudio out;
    out.sample_rate = dec.outputSampleRate;
    out.source_channels = static_cast<int>(dec.outputChannels);
    out.format = format_of(path);
    const ma_uint32 ch = dec.outputChannels;
    std::vector<float> buf(4096 * static_cast<std::size_t>(ch));
    const bool stereo = ch >= 2;
    for (;;) {
        ma_uint64 got = 0;
        if (ma_decoder_read_pcm_frames(&dec, buf.data(), 4096, &got) != MA_SUCCESS && got == 0) {
            break;
        }
        for (ma_uint64 i = 0; i < got; ++i) {
            const float* f = buf.data() + i * ch;
            if (ch <= 2) {
                out.left.push_back(f[0]);
                if (stereo) {
                    out.right.push_back(f[1]);
                }
                continue;
            }
            // More than two channels: front left/right plus the rest folded into both sides.
            float extra = 0.0f;
            for (ma_uint32 c = 2; c < ch; ++c) {
                extra += f[c];
            }
            extra *= 0.5f / static_cast<float>(ch - 2);
            out.left.push_back(f[0] + extra);
            out.right.push_back(f[1] + extra);
        }
        if (got < 4096) {
            break;
        }
    }
    ma_decoder_uninit(&dec);
    if (out.left.empty()) {
        if (error) {
            *error = "file contains no audio";
        }
        return std::nullopt;
    }
    return out;
}

std::optional<SmplInfo> read_smpl(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        return std::nullopt;
    }
    for (std::size_t pos = 12; pos + 8 <= b.size();) {
        const std::size_t size = le32(b.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (std::memcmp(b.data() + pos, "smpl", 4) == 0 && body + 36 <= b.size() && size >= 36) {
            SmplInfo s;
            s.unity_note = static_cast<int>(le32(b.data() + body + 12));
            if (s.unity_note > 127) {
                s.unity_note = -1;
            }
            const std::uint32_t loops = le32(b.data() + body + 28);
            if (loops > 0 && body + 36 + 24 <= b.size()) {
                const unsigned char* l = b.data() + body + 36;
                s.ping_pong = le32(l + 4) == 1;
                s.loop_start = le32(l + 8);
                s.loop_end = static_cast<std::size_t>(le32(l + 12)) + 1;
                s.has_loop = s.loop_end > s.loop_start + 1;
            }
            return s;
        }
        pos = body + size + (size & 1);
    }
    return std::nullopt;
}

std::vector<float> resample(const std::vector<float>& in, double from_hz, double to_hz) {
    if (in.empty() || from_hz == to_hz) {
        return in;
    }
    const auto out_len = static_cast<std::size_t>(std::llround(static_cast<double>(in.size()) * to_hz / from_hz));
    const int block = static_cast<int>(std::min<std::size_t>(in.size(), 1 << 16));
    r8b::CDSPResampler24 r(from_hz, to_hz, block);
    std::vector<double> src(in.begin(), in.end());
    std::vector<double> dst(out_len);
    r.oneshot(src.data(), static_cast<int>(src.size()), dst.data(), static_cast<int>(dst.size()));
    return std::vector<float>(dst.begin(), dst.end());
}

ImportResult import_sample(const std::filesystem::path& path, const ImportOptions& options) {
    ImportResult res;
    std::string error;
    auto audio = decode_file(path, &error);
    if (!audio) {
        res.error = error;
        return res;
    }
    const std::uint32_t from = audio->sample_rate;
    const std::uint32_t to = options.target_rate;
    const double ratio = static_cast<double>(to) / from;
    std::vector<float> left = resample(audio->left, from, to);
    std::vector<float> right = audio->right.empty() ? std::vector<float>{} : resample(audio->right, from, to);

    const std::string stem = path.stem().string();
    res.sample = model::make_sample(stem, path.string(), to, std::move(left), std::move(right));
    if (options.normalize) {
        res.sample = model::edit::normalize(*res.sample);
    }
    res.description = audio->format + " " + std::to_string(from) + " HZ " + std::to_string(audio->source_channels) + " CH";
    if (from != to) {
        res.description += " -> " + std::to_string(to) + " HZ";
    }

    // Root key: file name, then the smpl unity note, then pitch detection, then C4.
    const auto smpl = read_smpl(path);
    if (const auto n = note_from_filename(stem)) {
        res.params.root_note = *n;
        res.root_source = "NAME";
    } else if (smpl && smpl->unity_note >= 0 && smpl->unity_note != 60) {
        res.params.root_note = smpl->unity_note;
        res.root_source = "SMPL";
    } else {
        // YIN on up to 0.25 s from a tenth into the sound (past the attack).
        const model::SampleData& s = *res.sample;
        const std::size_t start = s.frames / 10;
        const std::size_t len = std::min<std::size_t>(s.frames - start, s.sample_rate / 4);
        std::vector<float> mono(len);
        for (std::size_t i = 0; i < len; ++i) {
            mono[i] = s.channels == 2 ? 0.5f * (s.left[start + i] + s.right[start + i]) : s.left[start + i];
        }
        const auto p = dsp::yin(mono, s.sample_rate);
        if (p.confidence > 0.8 && p.hz > 30.0) {
            res.params.root_note = std::clamp(static_cast<int>(std::lround(69.0 + 12.0 * std::log2(p.hz / 440.0))), 0, 127);
            res.params.fine_cents = 0.0f;
            res.root_source = "PITCH";
        } else {
            res.root_source = "DEFAULT";
        }
    }
    if (smpl && smpl->has_loop) {
        res.params.loop = smpl->ping_pong ? model::LoopMode::PingPong : model::LoopMode::Forward;
        res.params.loop_start = std::min(res.sample->frames, static_cast<std::size_t>(std::llround(static_cast<double>(smpl->loop_start) * ratio)));
        res.params.loop_end = std::min(res.sample->frames, static_cast<std::size_t>(std::llround(static_cast<double>(smpl->loop_end) * ratio)));
    } else {
        res.params.loop_start = 0;
        res.params.loop_end = res.sample->frames;
    }
    return res;
}

} // namespace phos::io
