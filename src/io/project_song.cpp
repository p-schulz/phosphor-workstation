// Song side of project.json: patterns, sections, tracks and (schemaVersion 4) loop slots,
// effects, sends and VAPOR; the shared bundle file helpers.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

#include "io/import.h"
#include "io/project_detail.h"
#include "io/wav.h"
#include "model/workspace.h"

namespace phos::io::detail {

namespace fs = std::filesystem;
using nlohmann::json;

bool fail(std::string* error, std::string text) {
    if (error) {
        *error = std::move(text);
    }
    return false;
}

std::string sanitize(const std::string& name) {
    std::string out;
    for (const char c : name) {
        out += std::isalnum(static_cast<unsigned char>(c)) || c == '-' ? c : '_';
    }
    return out.substr(0, 32);
}

bool write_sample(const std::filesystem::path& bundle, const std::string& rel, const model::SampleData& s,
                  std::string* error) {
    AudioBuffer buf;
    buf.sample_rate = s.sample_rate;
    buf.channels = static_cast<unsigned>(s.channels);
    buf.samples.resize(s.frames * buf.channels);
    for (std::size_t f = 0; f < s.frames; ++f) {
        buf.samples[f * buf.channels] = s.left[f];
        if (buf.channels == 2) {
            buf.samples[f * 2 + 1] = s.right[f];
        }
    }
    std::filesystem::path tmp = bundle / rel;
    tmp += ".tmp";
    if (!write_wav(tmp, buf, WavFormat::Float32, error)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, bundle / rel, ec);
    return ec ? fail(error, "cannot write " + rel) : true;
}

model::SampleRef read_sample(const std::filesystem::path& bundle, const std::string& rel, std::uint32_t engine_rate,
                             const std::string& name, const std::string& source, std::string* error, double* ratio) {
    std::string err;
    auto buf = read_wav(bundle / rel, &err);
    if (!buf) {
        fail(error, "missing sample " + rel + ": " + err);
        return nullptr;
    }
    std::vector<float> l(buf->frames());
    std::vector<float> r(buf->channels == 2 ? buf->frames() : 0);
    for (std::size_t f = 0; f < buf->frames(); ++f) {
        l[f] = buf->samples[f * buf->channels];
        if (!r.empty()) {
            r[f] = buf->samples[f * buf->channels + 1];
        }
    }
    if (ratio) {
        *ratio = static_cast<double>(engine_rate) / buf->sample_rate;
    }
    if (buf->sample_rate != engine_rate) {
        l = resample(l, buf->sample_rate, engine_rate);
        r = r.empty() ? r : resample(r, buf->sample_rate, engine_rate);
    }
    return model::make_sample(name, source, engine_rate, std::move(l), std::move(r));
}

std::string loop_file(int slot, const model::LoopSlot& loop, const std::string& audio_dir) {
    char prefix[24];
    std::snprintf(prefix, sizeof prefix, "loops/%02d_", slot + 1);
    return audio_dir + prefix + sanitize(loop.sample ? loop.sample->name : loop.name) + ".wav";
}

bool save_loop_samples(const fs::path& bundle, const model::Song& s, const std::string& audio_dir, std::string* error) {
    std::error_code ec;
    fs::create_directories(bundle / (audio_dir + "loops"), ec);
    std::set<std::string> written;
    for (int i = 0; i < model::kLoopSlots; ++i) {
        const model::LoopSlot& l = s.loops[static_cast<std::size_t>(i)];
        if (!l.sample) {
            continue;
        }
        const std::string rel = loop_file(i, l, audio_dir);
        if (!write_sample(bundle, rel, *l.sample, error)) {
            return false;
        }
        written.insert(fs::path(rel).filename().string());
    }
    for (const auto& e : fs::directory_iterator(bundle / (audio_dir + "loops"), ec)) {
        if (e.path().extension() == ".wav" && !written.count(e.path().filename().string())) {
            fs::remove(e.path(), ec);
        }
    }
    return true;
}

namespace {

constexpr std::array<const char*, 8> kGrid = {"1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T", "1/32T"};
constexpr std::array<const char*, 3> kMetronome = {"OFF", "PLAY", "RECORD"};
constexpr std::array<const char*, 2> kPlayMode = {"PATTERN", "SONG"};
constexpr std::array<const char*, 2> kTrackType = {"NOTES", "LOOPS"};
constexpr std::array<const char*, 3> kDest = {"INTERNAL", "EXTERNAL", "BOTH"};
constexpr std::array<const char*, 3> kSync = {"VARISPEED", "STRETCH", "SLICE"};
constexpr std::array<const char*, 3> kQuantize = {"IMMEDIATE", "BEAT", "BAR"};

json fx_json(const model::FxSlot& f) { return {{"type", model::fx_name(f.type)}, {"p", f.p}}; }

model::FxSlot fx_from(const json& j, model::FxSlot fallback) {
    if (!j.is_object()) {
        return fallback;
    }
    model::FxSlot f;
    if (!model::fx_from_name(j.value("type", std::string{"OFF"}), f.type)) {
        f.type = model::FxType::Off;
    }
    f = model::make_fx(f.type);
    const auto& p = j.value("p", json::array());
    for (std::size_t k = 0; k < p.size() && k < f.p.size(); ++k) {
        if (p[k].is_number()) {
            f.p[k] = p[k].get<float>();
        }
    }
    model::clamp_fx(f);
    return f;
}

// Effects, track types and sends, loops (without their audio), VAPOR (schemaVersion 4).
void mix_to_json(const model::Song& s, json& out, const std::string& audio_dir) {
    for (std::size_t t = 0; t < s.tracks.size(); ++t) {
        const model::Track& tr = s.tracks[t];
        json& tj = out["tracks"][t];
        tj["type"] = kTrackType[static_cast<std::size_t>(tr.type)];
        tj["inserts"] = json::array({fx_json(tr.inserts[0]), fx_json(tr.inserts[1])});
        tj["sendA"] = tr.send_a;
        tj["sendB"] = tr.send_b;
        tj["midi"] = {{"dest", kDest[static_cast<std::size_t>(tr.dest)]},
                      {"port", tr.out_port},
                      {"channel", tr.out_channel + 1},
                      {"program", tr.program < 0 ? 0 : tr.program + 1}}; // 0 = none
    }
    json loops = json::array();
    for (int i = 0; i < model::kLoopSlots; ++i) {
        const model::LoopSlot& l = s.loops[static_cast<std::size_t>(i)];
        loops.push_back({{"name", l.name},
                         {"sample", l.sample ? json(detail::loop_file(i, l, audio_dir)) : json(nullptr)},
                         {"bars", l.bars},
                         {"beatsPerBar", l.beats_per_bar},
                         {"sync", kSync[static_cast<std::size_t>(l.sync)]},
                         {"quantize", kQuantize[static_cast<std::size_t>(l.quantize)]},
                         {"shift", l.shift},
                         {"slices", l.slices},
                         {"gainDb", l.gain_db},
                         {"lpHz", l.lp_hz},
                         {"reverse", l.reverse},
                         {"screw", l.screw},
                         {"track", l.track}});
    }
    out["loops"] = loops;
    out["returns"] = json::array({fx_json(s.returns[0]), fx_json(s.returns[1])});
    out["returnLevel"] = s.return_level;
    out["masterFx"] = json::array({fx_json(s.master_fx[0]), fx_json(s.master_fx[1])});
    out["vapor"] = s.vapor;
}

void mix_from_json(const json& sj, model::Song& s) {
    const auto& tracks = sj.value("tracks", json::array());
    for (std::size_t i = 0; i < tracks.size() && i < static_cast<std::size_t>(model::kTracks); ++i) {
        model::Track& t = s.tracks[i];
        const json& tj = tracks[i];
        t.type = static_cast<model::TrackType>(index_of(kTrackType, tj, "type", 0));
        const auto& ins = tj.value("inserts", json::array());
        for (std::size_t k = 0; k < ins.size() && k < t.inserts.size(); ++k) {
            t.inserts[k] = fx_from(ins[k], model::FxSlot{});
        }
        t.send_a = std::clamp(tj.value("sendA", 0.0f), 0.0f, 1.0f);
        t.send_b = std::clamp(tj.value("sendB", 0.0f), 0.0f, 1.0f);
        if (const auto m = tj.find("midi"); m != tj.end() && m->is_object()) {
            t.dest = static_cast<model::MidiDest>(index_of(kDest, *m, "dest", 0));
            t.out_port = m->value("port", std::string{});
            t.out_channel = std::clamp(m->value("channel", static_cast<int>(i) + 1), 1, 16) - 1;
            t.program = std::clamp(m->value("program", 0), 0, 128) - 1;
        }
    }
    const auto& rets = sj.value("returns", json::array());
    for (std::size_t k = 0; k < rets.size() && k < s.returns.size(); ++k) {
        s.returns[k] = fx_from(rets[k], s.returns[k]);
    }
    const auto& lv = sj.value("returnLevel", json::array());
    for (std::size_t k = 0; k < lv.size() && k < s.return_level.size(); ++k) {
        s.return_level[k] = lv[k].is_number() ? std::clamp(lv[k].get<float>(), 0.0f, 1.0f) : 1.0f;
    }
    const auto& mfx = sj.value("masterFx", json::array());
    for (std::size_t k = 0; k < mfx.size() && k < s.master_fx.size(); ++k) {
        s.master_fx[k] = fx_from(mfx[k], model::FxSlot{});
    }
    s.vapor = std::clamp(sj.value("vapor", 0), 0, model::kVaporMax);
    const auto& loops = sj.value("loops", json::array());
    for (std::size_t i = 0; i < loops.size() && i < static_cast<std::size_t>(model::kLoopSlots); ++i) {
        model::LoopSlot& l = s.loops[i];
        const json& lj = loops[i];
        l.name = lj.value("name", std::string("EMPTY"));
        l.bars = std::clamp(lj.value("bars", 1), 1, 64);
        l.beats_per_bar = std::clamp(lj.value("beatsPerBar", 4), 1, 16);
        l.sync = static_cast<model::LoopSync>(index_of(kSync, lj, "sync", 0));
        l.quantize = static_cast<model::LaunchQuantize>(index_of(kQuantize, lj, "quantize", 2));
        l.shift = std::clamp(lj.value("shift", 0.0f), -24.0f, 24.0f);
        l.slices = std::clamp(lj.value("slices", 16), 2, 64);
        l.gain_db = std::clamp(lj.value("gainDb", 0.0f), -60.0f, 12.0f);
        l.lp_hz = std::clamp(lj.value("lpHz", 20000.0f), 200.0f, 20000.0f);
        l.reverse = lj.value("reverse", false);
        l.screw = std::clamp(lj.value("screw", 1.0f), 0.5f, 1.0f);
        l.track = std::clamp(lj.value("track", 8 + static_cast<int>(i)), 0, model::kTracks - 1);
    }
}

bool load_loop_samples(const fs::path& bundle, const json& sj, model::Song& s, std::uint32_t engine_rate,
                       std::string* error) {
    const auto& loops = sj.value("loops", json::array());
    for (std::size_t i = 0; i < loops.size() && i < static_cast<std::size_t>(model::kLoopSlots); ++i) {
        const json& lj = loops[i];
        if (!lj.contains("sample") || !lj["sample"].is_string()) {
            continue;
        }
        const std::string rel = lj["sample"].get<std::string>();
        s.loops[i].sample = detail::read_sample(bundle, rel, engine_rate, s.loops[i].name, rel, error);
        if (!s.loops[i].sample) {
            return false;
        }
    }
    return true;
}

} // namespace

json song_to_json(const model::Song& s, const std::string& audio_dir) {
    json patterns = json::array();
    for (const auto& p : s.patterns) {
        json notes = json::array();
        for (int t = 0; t < model::kTracks; ++t) {
            for (const auto& n : p.lanes[static_cast<std::size_t>(t)]) {
                notes.push_back({t, n.tick, n.length, n.note, n.velocity}); // compact: track, tick, length, note, vel
            }
        }
        json pj = {{"name", p.name}, {"steps", p.steps}, {"grid", kGrid[static_cast<std::size_t>(p.grid)]}, {"notes", notes}};
        json autos = json::array();
        for (const auto& l : p.automation) {
            autos.push_back({{"track", l.track}, {"cc", l.cc}, {"values", l.values}});
        }
        pj["automation"] = autos;
        patterns.push_back(pj);
    }
    json sections = json::array();
    for (const auto& sec : s.sections) {
        sections.push_back({{"pattern", sec.pattern}, {"repeats", sec.repeats}, {"bpm10", sec.bpm10},
                            {"sigNum", sec.sig_num}, {"sigDen", sec.sig_den}});
    }
    json tracks = json::array();
    for (const auto& t : s.tracks) {
        tracks.push_back({{"name", t.name}, {"instrument", t.instrument}, {"volumeDb", t.volume_db}, {"pan", t.pan},
                          {"mute", t.mute}, {"solo", t.solo}, {"swing", t.swing}});
    }
    json out = {
        {"patterns", patterns},
        {"sections", sections},
        {"order", s.order},
        {"tracks", tracks},
        {"swing", s.swing},
        {"swingGrid", kGrid[static_cast<std::size_t>(s.swing_grid)]},
        {"metronome", kMetronome[static_cast<std::size_t>(s.metronome)]},
        {"countInBars", s.count_in_bars},
        {"playMode", kPlayMode[static_cast<std::size_t>(s.play_mode)]},
        {"recordQuantize", kGrid[static_cast<std::size_t>(s.record_quantize)]},
        {"quantizeOnRecord", s.quantize_on_record},
        {"recordOffsetMs", s.record_offset_ms},
        {"masterDb", s.master_db},
        {"currentSection", s.current_section},
        {"currentTrack", s.current_track},
        {"snesEcho", {{"delay", s.snes_echo.delay}, {"feedback", s.snes_echo.feedback}, {"volL", s.snes_echo.vol_l},
                      {"volR", s.snes_echo.vol_r}, {"fir", s.snes_echo.fir}, {"noiseRate", s.snes_echo.noise_rate}}},
        {"strict", {{"cmi", s.strict_cmi}, {"gb", s.strict_gb}, {"snes", s.strict_snes}}},
    };
    mix_to_json(s, out, audio_dir);
    return out;
}

bool song_from_json(const json& sj, model::Song& s, const fs::path& bundle, std::uint32_t engine_rate,
                    std::string* error) {
    s.patterns.clear();
    for (const auto& pj : sj.value("patterns", json::array())) {
        model::Pattern p;
        p.name = pj.value("name", std::string("PATTERN"));
        p.steps = std::clamp(pj.value("steps", 16), 1, 256);
        p.grid = static_cast<model::Grid>(index_of(kGrid, pj, "grid", 2));
        for (const auto& n : pj.value("notes", json::array())) {
            if (!n.is_array() || n.size() != 5) {
                continue;
            }
            const int track = std::clamp(n[0].get<int>(), 0, model::kTracks - 1);
            model::insert_note(p.lanes[static_cast<std::size_t>(track)],
                               model::Note{n[1].get<std::uint32_t>(), n[2].get<std::uint32_t>(),
                                           static_cast<std::uint8_t>(n[3].get<int>() & 0x7F),
                                           static_cast<std::uint8_t>(std::clamp(n[4].get<int>(), 1, 127))});
        }
        for (const auto& aj : pj.value("automation", json::array())) {
            model::AutomationLane l;
            l.track = std::clamp(aj.value("track", 0), 0, model::kTracks - 1);
            l.cc = std::clamp(aj.value("cc", 7), 0, 127);
            for (const auto& v : aj.value("values", json::array())) {
                l.values.push_back(static_cast<std::int16_t>(v.is_number() ? std::clamp(v.get<int>(), -1, 127) : -1));
            }
            l.values.resize(static_cast<std::size_t>(p.steps), -1);
            if (!p.lane(l.track, l.cc)) {
                p.automation.push_back(std::move(l));
            }
        }
        s.patterns.push_back(std::move(p));
        if (s.patterns.size() >= static_cast<std::size_t>(model::kMaxPatterns)) {
            break;
        }
    }
    if (s.patterns.empty()) {
        s.patterns.push_back(model::Pattern{"PATTERN 01"});
    }
    const auto& secs = sj.value("sections", json::array());
    for (std::size_t i = 0; i < secs.size() && i < static_cast<std::size_t>(model::kSections); ++i) {
        model::Section& sec = s.sections[i];
        sec.pattern = std::clamp(secs[i].value("pattern", 0), 0, static_cast<int>(s.patterns.size()) - 1);
        sec.repeats = std::clamp(secs[i].value("repeats", 1), 1, 99);
        sec.bpm10 = std::clamp(secs[i].value("bpm10", 1200), 200, 3000);
        sec.sig_num = std::clamp(secs[i].value("sigNum", 4), 1, 16);
        const int den = secs[i].value("sigDen", 4);
        sec.sig_den = den == 2 || den == 8 || den == 16 ? den : 4;
    }
    s.order.clear();
    for (const auto& o : sj.value("order", json::array({0}))) {
        s.order.push_back(std::clamp(o.get<int>(), 0, model::kSections - 1));
    }
    if (s.order.empty()) {
        s.order.push_back(0);
    }
    const auto& tracks = sj.value("tracks", json::array());
    for (std::size_t i = 0; i < tracks.size() && i < static_cast<std::size_t>(model::kTracks); ++i) {
        model::Track& t = s.tracks[i];
        t.name = tracks[i].value("name", t.name);
        t.instrument = std::clamp(tracks[i].value("instrument", t.instrument), 0, model::kInstruments - 1);
        t.volume_db = tracks[i].value("volumeDb", 0.0f);
        t.pan = tracks[i].value("pan", 0.0f);
        t.mute = tracks[i].value("mute", false);
        t.solo = tracks[i].value("solo", false);
        t.swing = tracks[i].value("swing", false);
    }
    s.swing = std::clamp(sj.value("swing", 50), 50, 75);
    s.swing_grid = static_cast<model::Grid>(index_of(kGrid, sj, "swingGrid", 2));
    s.metronome = static_cast<model::MetronomeMode>(index_of(kMetronome, sj, "metronome", 2));
    s.count_in_bars = std::clamp(sj.value("countInBars", 1), 0, 2);
    s.play_mode = static_cast<model::PlayMode>(index_of(kPlayMode, sj, "playMode", 0));
    s.record_quantize = static_cast<model::Grid>(index_of(kGrid, sj, "recordQuantize", 2));
    s.quantize_on_record = sj.value("quantizeOnRecord", false);
    s.record_offset_ms = std::clamp(sj.value("recordOffsetMs", 0), -200, 200);
    s.master_db = sj.value("masterDb", 0.0f);
    s.current_section = std::clamp(sj.value("currentSection", 0), 0, model::kSections - 1);
    s.current_track = std::clamp(sj.value("currentTrack", 0), 0, model::kTracks - 1);
    if (const auto ej = sj.find("snesEcho"); ej != sj.end() && ej->is_object()) {
        s.snes_echo.delay = std::clamp(ej->value("delay", 4), 0, 15);
        s.snes_echo.feedback = std::clamp(ej->value("feedback", 50), -128, 127);
        s.snes_echo.vol_l = std::clamp(ej->value("volL", 40), -128, 127);
        s.snes_echo.vol_r = std::clamp(ej->value("volR", 40), -128, 127);
        const auto& fir = ej->value("fir", json::array());
        for (std::size_t k = 0; k < fir.size() && k < 8; ++k) {
            s.snes_echo.fir[k] = std::clamp(fir[k].get<int>(), -128, 127);
        }
        s.snes_echo.noise_rate = std::clamp(ej->value("noiseRate", 0x1C), 0, 31);
    }
    if (const auto st = sj.find("strict"); st != sj.end() && st->is_object()) {
        s.strict_cmi = st->value("cmi", false);
        s.strict_gb = st->value("gb", false);
        s.strict_snes = st->value("snes", false);
    }
    mix_from_json(sj, s);
    return load_loop_samples(bundle, sj, s, engine_rate, error);
}

} // namespace phos::io::detail
