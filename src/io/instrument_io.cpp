// Instruments in project.json and in instrument bundles (NAME.phinst/): parameters as JSON, audio
// (sample, wavetable, BRR) as files next to it. Shared by projects and the instrument library.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "io/import.h"
#include "io/instrument_io.h"
#include "io/project.h"
#include "io/project_detail.h"
#include "io/wav.h"

namespace phos::io {

namespace fs = std::filesystem;
using nlohmann::json;

namespace detail {

namespace {

constexpr std::array<const char*, 3> kInterp = {"DROP", "LINEAR", "HERMITE"};
constexpr std::array<const char*, 3> kLoop = {"OFF", "FORWARD", "PINGPONG"};
constexpr std::array<const char*, 4> kFilter = {"OFF", "LOWPASS", "HIGHPASS", "BANDPASS"};
constexpr std::array<const char*, 6> kKind = {"SAMPLER", "WAVE", "SYNTH", "DRUM", "GB", "SNES"};
constexpr std::array<const char*, 3> kGbChannel = {"PULSE", "WAVE", "NOISE"};
constexpr std::array<const char*, 5> kGain = {"DIRECT", "LINDEC", "EXPDEC", "LININC", "BENTINC"};

// `dir`: "" for project.json, kAutosaveDir for the autosave (its own copy of the audio).
std::string brr_file(int index, const std::string& dir) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "brr/%02d.brr", index + 1);
    return dir + buf;
}
constexpr std::array<const char*, 6> kOsc = {"SAW", "PULSE", "TRIANGLE", "SINE", "NOISE", "OFF"};
constexpr std::array<const char*, 4> kLfo = {"SINE", "TRIANGLE", "SQUARE", "SH"};
constexpr std::array<const char*, 8> kDrum = {"OFF", "KICK", "SNARE", "HAT", "CLAP", "CHIP", "TOM", "SAMPLE"};

json gb_json(const model::GbParams& g) {
    json macros = json::array();
    for (const auto& m : g.macros) {
        macros.push_back({{"steps", m.steps}, {"loop", m.loop}, {"release", m.release}});
    }
    json waves = json::array();
    for (const auto& w : g.waves) {
        waves.push_back(w);
    }
    return {{"channel", kGbChannel[static_cast<std::size_t>(g.channel)]}, {"duty", g.duty}, {"sweepPace", g.sweep_pace},
            {"sweepDown", g.sweep_down}, {"sweepShift", g.sweep_shift}, {"envVolume", g.env_volume},
            {"envUp", g.env_up}, {"envPace", g.env_pace}, {"length", g.length}, {"waveLevel", g.wave_level},
            {"waves", waves}, {"noiseShort", g.noise_short}, {"quantize", g.quantize}, {"raw", g.raw},
            {"vibratoCents", g.vibrato_cents}, {"vibratoSpeed", g.vibrato_speed}, {"vibratoDelay", g.vibrato_delay},
            {"portaTicks", g.porta_ticks}, {"macros", macros}};
}

model::GbParams gb_from(const json& j) {
    model::GbParams g = model::default_gb(model::GbChannel::Pulse);
    g.channel = static_cast<model::GbChannel>(index_of(kGbChannel, j, "channel", 0));
    g.duty = std::clamp(j.value("duty", g.duty), 0, 3);
    g.sweep_pace = std::clamp(j.value("sweepPace", 0), 0, 7);
    g.sweep_down = j.value("sweepDown", false);
    g.sweep_shift = std::clamp(j.value("sweepShift", 0), 0, 7);
    g.env_volume = std::clamp(j.value("envVolume", 15), 0, 15);
    g.env_up = j.value("envUp", false);
    g.env_pace = std::clamp(j.value("envPace", 0), 0, 7);
    g.length = std::clamp(j.value("length", 0), 0, 256);
    g.wave_level = std::clamp(j.value("waveLevel", 1), 0, 3);
    const auto& waves = j.value("waves", json::array());
    for (std::size_t w = 0; w < waves.size() && w < g.waves.size(); ++w) {
        for (std::size_t i = 0; i < waves[w].size() && i < 32; ++i) {
            g.waves[w][i] = static_cast<std::uint8_t>(std::clamp(waves[w][i].get<int>(), 0, 15));
        }
    }
    g.noise_short = j.value("noiseShort", false);
    g.quantize = j.value("quantize", true);
    g.raw = j.value("raw", false);
    g.vibrato_cents = j.value("vibratoCents", 0);
    g.vibrato_speed = j.value("vibratoSpeed", 6);
    g.vibrato_delay = j.value("vibratoDelay", 0);
    g.porta_ticks = j.value("portaTicks", 0);
    const auto& macros = j.value("macros", json::array());
    for (std::size_t m = 0; m < macros.size() && m < g.macros.size(); ++m) {
        model::GbMacro mac;
        for (const auto& v : macros[m].value("steps", json::array())) {
            mac.steps.push_back(static_cast<std::int8_t>(std::clamp(v.get<int>(), -128, 127)));
        }
        mac.loop = macros[m].value("loop", -1);
        mac.release = macros[m].value("release", -1);
        g.macros[m] = mac;
    }
    return g;
}

json snes_json(const model::SnesParams& s) {
    return {{"adsr", s.adsr}, {"attack", s.attack}, {"decay", s.decay}, {"sustainLevel", s.sustain_level},
            {"sustainRate", s.sustain_rate}, {"gainMode", kGain[static_cast<std::size_t>(s.gain_mode)]}, {"gain", s.gain},
            {"volL", s.vol_l}, {"volR", s.vol_r}, {"noise", s.noise}, {"pitchMod", s.pitch_mod}, {"echo", s.echo},
            {"rootNote", s.root_note}, {"previewOriginal", s.preview_original}, {"sourceRate", s.source_rate},
            {"autoAlign", s.auto_align}};
}

model::SnesParams snes_from(const json& j) {
    model::SnesParams s;
    s.adsr = j.value("adsr", true);
    s.attack = std::clamp(j.value("attack", 15), 0, 15);
    s.decay = std::clamp(j.value("decay", 7), 0, 7);
    s.sustain_level = std::clamp(j.value("sustainLevel", 7), 0, 7);
    s.sustain_rate = std::clamp(j.value("sustainRate", 0), 0, 31);
    s.gain_mode = static_cast<model::SnesGainMode>(index_of(kGain, j, "gainMode", 0));
    s.gain = std::clamp(j.value("gain", 127), 0, 127);
    s.vol_l = std::clamp(j.value("volL", 100), -128, 127);
    s.vol_r = std::clamp(j.value("volR", 100), -128, 127);
    s.noise = j.value("noise", false);
    s.pitch_mod = j.value("pitchMod", false);
    s.echo = j.value("echo", true);
    s.root_note = std::clamp(j.value("rootNote", 60), 0, 127);
    s.preview_original = j.value("previewOriginal", false);
    s.source_rate = std::clamp(j.value("sourceRate", 32000), 8000, 32000);
    s.auto_align = j.value("autoAlign", true);
    return s;
}

std::string wavetable_file(int index, const std::string& dir) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "wavetables/%02d.wav", index + 1);
    return dir + buf;
}

json synth_json(const model::SynthParams& s) {
    return {{"osc1", kOsc[static_cast<std::size_t>(s.osc1)]}, {"osc2", kOsc[static_cast<std::size_t>(s.osc2)]},
            {"osc2Semitones", s.osc2_semitones}, {"osc2Detune", s.osc2_detune}, {"osc1Level", s.osc1_level},
            {"osc2Level", s.osc2_level}, {"pulseWidth", s.pulse_width}, {"subLevel", s.sub_level},
            {"noiseLevel", s.noise_level}, {"unison", s.unison}, {"unisonSpread", s.unison_spread},
            {"lfoShape", kLfo[static_cast<std::size_t>(s.lfo_shape)]}, {"lfoRateHz", s.lfo_rate_hz},
            {"lfoSync", s.lfo_sync}, {"lfoToPitch", s.lfo_to_pitch}, {"lfoToCutoff", s.lfo_to_cutoff},
            {"lfoToPw", s.lfo_to_pw}, {"lfoToAmp", s.lfo_to_amp}, {"glideMs", s.glide_ms}, {"mono", s.mono}};
}

model::SynthParams synth_from(const json& j) {
    model::SynthParams s;
    const model::SynthParams d;
    s.osc1 = static_cast<model::OscWave>(index_of(kOsc, j, "osc1", 0));
    s.osc2 = static_cast<model::OscWave>(index_of(kOsc, j, "osc2", 1));
    s.osc2_semitones = std::clamp(j.value("osc2Semitones", d.osc2_semitones), -24, 24);
    s.osc2_detune = j.value("osc2Detune", d.osc2_detune);
    s.osc1_level = j.value("osc1Level", d.osc1_level);
    s.osc2_level = j.value("osc2Level", d.osc2_level);
    s.pulse_width = j.value("pulseWidth", d.pulse_width);
    s.sub_level = j.value("subLevel", d.sub_level);
    s.noise_level = j.value("noiseLevel", d.noise_level);
    s.unison = std::clamp(j.value("unison", d.unison), 1, 4);
    s.unison_spread = j.value("unisonSpread", d.unison_spread);
    s.lfo_shape = static_cast<model::LfoShape>(index_of(kLfo, j, "lfoShape", 0));
    s.lfo_rate_hz = j.value("lfoRateHz", d.lfo_rate_hz);
    s.lfo_sync = std::clamp(j.value("lfoSync", d.lfo_sync), 0, 64);
    s.lfo_to_pitch = j.value("lfoToPitch", d.lfo_to_pitch);
    s.lfo_to_cutoff = j.value("lfoToCutoff", d.lfo_to_cutoff);
    s.lfo_to_pw = j.value("lfoToPw", d.lfo_to_pw);
    s.lfo_to_amp = j.value("lfoToAmp", d.lfo_to_amp);
    s.glide_ms = j.value("glideMs", d.glide_ms);
    s.mono = j.value("mono", d.mono);
    return s;
}


std::string sample_file(int index, const model::Instrument& inst, const std::string& dir) {
    char prefix[16];
    std::snprintf(prefix, sizeof prefix, "%02d_", index + 1);
    return dir + "samples/" + prefix + sanitize(inst.sample->name) + ".wav";
}

json adsr_json(const model::Adsr& a) {
    return {{"attackMs", a.attack_ms}, {"decayMs", a.decay_ms}, {"sustain", a.sustain}, {"releaseMs", a.release_ms}};
}

model::Adsr adsr_from(const json& j, model::Adsr d) {
    if (!j.is_object()) {
        return d;
    }
    return {j.value("attackMs", d.attack_ms), j.value("decayMs", d.decay_ms), j.value("sustain", d.sustain),
            j.value("releaseMs", d.release_ms)};
}

} // namespace

json instrument_json(const model::Instrument& inst, int i, const std::string& audio_dir) {
    const model::SamplerParams& p = inst.sampler;
    json o = {
        {"name", inst.name},
        {"rootNote", p.root_note},
        {"fineCents", p.fine_cents},
        {"volumeDb", p.volume_db},
        {"pan", p.pan},
        {"interp", kInterp[static_cast<std::size_t>(p.interp)]},
        {"loop", kLoop[static_cast<std::size_t>(p.loop)]},
        {"loopStart", p.loop_start},
        {"loopEnd", p.loop_end},
        {"amp", adsr_json(p.amp)},
        {"filter", kFilter[static_cast<std::size_t>(p.filter)]},
        {"cutoffHz", p.cutoff_hz},
        {"resonance", p.resonance},
        {"envAmount", p.env_amount},
        {"keyTrack", p.key_track},
        {"filterEnv", adsr_json(p.filter_env)},
        {"velToAmp", p.vel_to_amp},
        {"velToCutoff", p.vel_to_cutoff},
    };
    o["kind"] = kKind[static_cast<std::size_t>(inst.kind)];
    if (inst.kind == model::InstrumentKind::Wavetable && inst.wavetable) {
        json segs = json::array();
        for (const auto& seg : inst.wavetable->segments) {
            segs.push_back({{"cycles", seg.cycles}, {"harmonics", seg.harmonics}});
        }
        o["wavetable"] = {{"file", wavetable_file(i, audio_dir)}, {"segments", segs}, {"loopStart", inst.wave.loop_start},
                          {"loopLength", inst.wave.loop_length}, {"crossfade", inst.wave.crossfade},
                          {"authentic", inst.wave.authentic}};
    }
    if (inst.kind == model::InstrumentKind::Synth) {
        o["synth"] = synth_json(inst.synth);
    }
    if (inst.kind == model::InstrumentKind::Gb) {
        o["gb"] = gb_json(inst.gb);
    }
    if (inst.kind == model::InstrumentKind::Snes) {
        o["snes"] = snes_json(inst.snes);
        if (inst.brr) {
            o["brr"] = {{"file", brr_file(i, audio_dir)}, {"loopBlock", inst.brr->loop_block}, {"sourceRate", inst.brr->source_rate}};
        }
    }
    if (inst.kind == model::InstrumentKind::Drum) {
        json pads = json::array();
        for (const auto& pad : inst.drum) {
            pads.push_back({{"type", kDrum[static_cast<std::size_t>(pad.type)]}, {"tune", pad.tune},
                            {"decayMs", pad.decay_ms}, {"tone", pad.tone}, {"levelDb", pad.level_db},
                            {"pan", pad.pan}, {"choke", pad.choke}, {"sampleSlot", pad.sample_slot}});
        }
        o["drum"] = pads;
    }
    if (inst.sample) {
        o["sample"] = sample_file(i, inst, audio_dir);
        o["sampleRate"] = inst.sample->sample_rate;
        o["source"] = inst.sample->source_path;
        o["sampleName"] = inst.sample->name;
    } else {
        o["sample"] = nullptr;
    }
    return o;
}

bool write_instrument_files(const std::filesystem::path& bundle, const model::Instrument& inst, int i,
                            const std::string& dir, std::set<std::string>& written, std::string* error) {
    std::error_code ec;
    if (inst.sample) {
        const std::string rel = sample_file(i, inst, dir);
        if (!write_sample(bundle, rel, *inst.sample, error)) {
            return false;
        }
        written.insert(fs::path(rel).filename().string());
    }
    fs::create_directories(bundle / (dir + "brr"), ec);
    if (inst.kind == model::InstrumentKind::Snes && inst.brr) {
        std::ofstream out(bundle / brr_file(i, dir), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(inst.brr->data.data()), static_cast<std::streamsize>(inst.brr->data.size()));
        if (!out) {
            return fail(error, "cannot write " + brr_file(i, dir));
        }
    }
    fs::create_directories(bundle / (dir + "wavetables"), ec);
    if (inst.kind == model::InstrumentKind::Wavetable && inst.wavetable) {
        AudioBuffer buf;
        buf.sample_rate = 48000;
        buf.channels = 1;
        for (const auto& seg : inst.wavetable->segments) {
            buf.samples.insert(buf.samples.end(), seg.wave.begin(), seg.wave.end());
        }
        if (!write_wav(bundle / wavetable_file(i, dir), buf, WavFormat::Float32, error)) {
            return false;
        }
    }
    return true;
}

bool instrument_from_json(const json& o, model::Instrument& inst, const std::filesystem::path& bundle,
                          std::uint32_t engine_rate, std::string* error) {
    model::SamplerParams& p = inst.sampler;
    const model::SamplerParams d;
    inst.name = o.value("name", std::string("EMPTY"));
    p.root_note = std::clamp(o.value("rootNote", d.root_note), 0, 127);
    p.fine_cents = o.value("fineCents", d.fine_cents);
    p.volume_db = o.value("volumeDb", d.volume_db);
    p.pan = o.value("pan", d.pan);
    p.interp = static_cast<model::Interp>(index_of(kInterp, o, "interp", 2));
    p.loop = static_cast<model::LoopMode>(index_of(kLoop, o, "loop", 0));
    p.loop_start = o.value("loopStart", d.loop_start);
    p.loop_end = o.value("loopEnd", d.loop_end);
    p.amp = adsr_from(o.value("amp", json{}), d.amp);
    p.filter = static_cast<model::FilterType>(index_of(kFilter, o, "filter", 0));
    p.cutoff_hz = o.value("cutoffHz", d.cutoff_hz);
    p.resonance = o.value("resonance", d.resonance);
    p.env_amount = o.value("envAmount", d.env_amount);
    p.key_track = o.value("keyTrack", d.key_track);
    p.filter_env = adsr_from(o.value("filterEnv", json{}), d.filter_env);
    p.vel_to_amp = o.value("velToAmp", d.vel_to_amp);
    p.vel_to_cutoff = o.value("velToCutoff", d.vel_to_cutoff);
    inst.kind = static_cast<model::InstrumentKind>(index_of(kKind, o, "kind", 0));
    if (const auto w = o.find("wavetable"); w != o.end() && w->is_object()) {
        model::WavetableData data;
        std::string err;
        const auto wav = read_wav(bundle / w->value("file", std::string{}), &err);
        const auto& segs = w->value("segments", json::array());
        for (std::size_t s = 0; s < segs.size() && s < static_cast<std::size_t>(model::WavetableData::kMaxSegments); ++s) {
            model::WaveSegment seg;
            seg.cycles = std::clamp(segs[s].value("cycles", 4), 1, 255);
            const auto& h = segs[s].value("harmonics", json::array());
            for (std::size_t k = 0; k < h.size() && k < seg.harmonics.size(); ++k) {
                seg.harmonics[k] = h[k].get<float>();
            }
            for (std::size_t pt = 0; pt < seg.wave.size(); ++pt) {
                const std::size_t at = s * seg.wave.size() + pt;
                seg.wave[pt] = wav && at < wav->samples.size() ? wav->samples[at] : 0.0f;
            }
            data.segments.push_back(seg);
        }
        inst.wavetable = std::make_shared<const model::WavetableData>(std::move(data));
        inst.wave.loop_start = w->value("loopStart", 0);
        inst.wave.loop_length = w->value("loopLength", 0);
        inst.wave.crossfade = w->value("crossfade", true);
        inst.wave.authentic = w->value("authentic", false);
    }
    if (const auto g = o.find("gb"); g != o.end() && g->is_object()) {
        inst.gb = gb_from(*g);
    }
    if (const auto sn = o.find("snes"); sn != o.end() && sn->is_object()) {
        inst.snes = snes_from(*sn);
    }
    if (const auto bj = o.find("brr"); bj != o.end() && bj->is_object()) {
        std::ifstream bin(bundle / bj->value("file", std::string{}), std::ios::binary);
        auto brr = std::make_shared<model::BrrSample>();
        brr->data.assign(std::istreambuf_iterator<char>(bin), std::istreambuf_iterator<char>());
        brr->data.resize(brr->data.size() / 9 * 9);
        brr->loop_block = bj->value("loopBlock", -1);
        brr->source_rate = std::clamp(bj->value("sourceRate", 32000), 1000, 64000);
        if (!brr->data.empty()) {
            inst.brr = brr;
        }
    }
    if (const auto sj2 = o.find("synth"); sj2 != o.end() && sj2->is_object()) {
        inst.synth = synth_from(*sj2);
    }
    if (const auto dj = o.find("drum"); dj != o.end() && dj->is_array()) {
        for (std::size_t pi = 0; pi < dj->size() && pi < inst.drum.size(); ++pi) {
            const json& pj = (*dj)[pi];
            model::DrumPad& pad = inst.drum[pi];
            pad.type = static_cast<model::DrumType>(index_of(kDrum, pj, "type", 0));
            pad.tune = pj.value("tune", 0.0f);
            pad.decay_ms = pj.value("decayMs", 200.0f);
            pad.tone = pj.value("tone", 0.5f);
            pad.level_db = pj.value("levelDb", 0.0f);
            pad.pan = pj.value("pan", 0.0f);
            pad.choke = std::clamp(pj.value("choke", 0), 0, 4);
            pad.sample_slot = std::clamp(pj.value("sampleSlot", -1), -1, model::kInstruments - 1);
        }
    }
    if (o.contains("sample") && o["sample"].is_string()) {
        const std::string rel = o["sample"].get<std::string>();
        const std::string stem = fs::path(rel).stem().string().substr(3); // without "NN_"
        double ratio = 1.0;
        inst.sample = detail::read_sample(bundle, rel, engine_rate, o.value("sampleName", stem),
                                          o.value("source", std::string{}), error, &ratio);
        if (!inst.sample) {
            return false;
        }
        if (ratio != 1.0) {
            p.loop_start = static_cast<std::size_t>(std::llround(static_cast<double>(p.loop_start) * ratio));
            p.loop_end = static_cast<std::size_t>(std::llround(static_cast<double>(p.loop_end) * ratio));
        }
        p.loop_start = std::min(p.loop_start, inst.sample->frames);
        p.loop_end = std::min(p.loop_end, inst.sample->frames);
    }
    return true;
}

} // namespace detail

bool save_instrument(const fs::path& bundle, const model::Instrument& inst, std::string* error) {
    std::error_code ec;
    fs::remove_all(bundle / "samples", ec); // a re-export replaces the old audio
    fs::create_directories(bundle / "samples", ec);
    if (ec) {
        return detail::fail(error, "cannot create " + bundle.string());
    }
    std::set<std::string> written;
    if (!detail::write_instrument_files(bundle, inst, 0, {}, written, error)) {
        return false;
    }
    json j;
    j["schemaVersion"] = kProjectSchemaVersion;
    j["app"] = "PHOSPHOR";
    j["type"] = "instrument";
    j["instrument"] = detail::instrument_json(inst, 0, {});
    return detail::write_text(bundle / kInstrumentFile, j.dump(2) + "\n", error);
}

std::optional<model::Instrument> load_instrument(const fs::path& bundle, std::uint32_t engine_rate, std::string* error) {
    std::ifstream in(bundle / kInstrumentFile, std::ios::binary);
    if (!in) {
        detail::fail(error, "no instrument in " + bundle.filename().string());
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("instrument")) {
        detail::fail(error, bundle.filename().string() + " is not a PHOSPHOR instrument");
        return std::nullopt;
    }
    // Older instruments migrate like a project with this one instrument.
    std::string text = json{{"schemaVersion", j.value("schemaVersion", 0)}, {"instruments", json::array({j["instrument"]})}}.dump();
    if (!migrate_project(text, error)) {
        return std::nullopt;
    }
    const json m = json::parse(text);
    model::Instrument inst;
    if (!detail::instrument_from_json(m["instruments"][0], inst, bundle, engine_rate, error)) {
        return std::nullopt;
    }
    return inst;
}

fs::path instrument_path(const fs::path& library, const std::string& name_or_path) {
    fs::path p = name_or_path;
    if (p.extension() == kInstrumentExt && (p.is_absolute() || p.has_parent_path())) {
        return p;
    }
    if (p.extension() != kInstrumentExt) {
        p += kInstrumentExt;
    }
    return p.is_absolute() ? p : library / p;
}

bool is_instrument_bundle(const fs::path& path) {
    std::error_code ec;
    return path.extension() == kInstrumentExt && fs::is_directory(path, ec);
}

std::vector<fs::path> list_instruments(const fs::path& folder) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        if (is_instrument_bundle(e.path())) {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace phos::io
