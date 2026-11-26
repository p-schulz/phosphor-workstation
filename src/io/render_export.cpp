#include "io/render_export.h"

#include <cctype>

#include "engine/offline.h"
#include "io/wav.h"

namespace phos::io {

namespace {

bool write(const std::filesystem::path& path, const std::vector<float>& l, const std::vector<float>& r,
           const ExportOptions& opt, std::string* error) {
    AudioBuffer b;
    b.sample_rate = opt.sample_rate;
    b.channels = 2;
    b.samples.resize(l.size() * 2);
    for (std::size_t i = 0; i < l.size(); ++i) {
        b.samples[2 * i] = l[i];
        b.samples[2 * i + 1] = r[i];
    }
    return write_wav(path, b, opt.float32 ? WavFormat::Float32 : WavFormat::Pcm24, error);
}

std::string file_part(const std::string& name) {
    std::string out;
    for (const char c : name) {
        out += std::isalnum(static_cast<unsigned char>(c)) || c == '-' ? c : '_';
    }
    return out.substr(0, 24);
}

} // namespace

ExportResult export_audio(const model::Document& doc, const std::filesystem::path& out, const ExportOptions& opt) {
    ExportResult res;
    std::error_code ec;
    if (out.has_parent_path()) {
        std::filesystem::create_directories(out.parent_path(), ec);
    }
    engine::SongRender so;
    so.tail_s = opt.tail_s;
    so.stems = opt.stems;
    so.sample_rate = opt.sample_rate;
    const engine::Render r = engine::render_song(doc, so);
    res.seconds = static_cast<double>(r.left.size()) / opt.sample_rate;
    if (!write(out, r.left, r.right, opt, &res.error)) {
        return res;
    }
    res.files.push_back(out);
    for (const auto& stem : r.stems) {
        const auto& tr = doc.song.tracks[static_cast<std::size_t>(stem.track)];
        const std::string name = out.stem().string() + "_" + model::track_label(stem.track) + "_" + file_part(tr.name) + ".wav";
        const auto path = out.parent_path() / name;
        if (!write(path, stem.left, stem.right, opt, &res.error)) {
            return res;
        }
        res.files.push_back(path);
    }
    res.ok = true;
    return res;
}

} // namespace phos::io
