#include "content/presets.h"

#include <algorithm>
#include <cmath>

#include "engine/offline.h"
#include "io/snes.h"
#include "model/chip.h"

namespace phos::content::presets {

using namespace phos::model;

void note(Pattern& p, int track, double beat, double beats, int midi, int velocity) {
    insert_note(p.lanes[static_cast<std::size_t>(track)],
                Note{static_cast<std::uint32_t>(beat * kPpq), static_cast<std::uint32_t>(beats * kPpq),
                     static_cast<std::uint8_t>(midi), static_cast<std::uint8_t>(velocity)});
}

void chord(Pattern& p, int track, double beat, double beats, std::initializer_list<int> notes, int velocity) {
    for (const int n : notes) {
        note(p, track, beat, beats, n, velocity);
    }
}

Instrument electric_piano() {
    Instrument i = make_instrument(InstrumentKind::Synth, "E.PIANO");
    i.synth.osc1 = OscWave::Triangle;
    i.synth.osc2 = OscWave::Sine;
    i.synth.osc2_semitones = 12;
    i.synth.osc2_level = 0.25f;
    i.sampler.amp = Adsr{4.0f, 1200.0f, 0.35f, 700.0f};
    i.sampler.cutoff_hz = 2800.0f;
    i.sampler.resonance = 0.1f;
    i.sampler.env_amount = 1.2f;
    i.sampler.filter_env = Adsr{2.0f, 500.0f, 0.2f, 500.0f};
    i.sampler.key_track = 0.4f;
    i.sampler.volume_db = -4.0f;
    return i;
}

Instrument pad(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Saw;
    i.synth.osc2 = OscWave::Saw;
    i.synth.osc2_detune = 10.0f;
    i.synth.osc2_level = 0.8f;
    i.synth.unison = 3;
    i.synth.unison_spread = 14.0f;
    i.synth.lfo_rate_hz = 0.2f;
    i.synth.lfo_to_cutoff = 0.3f;
    i.sampler.amp = Adsr{400.0f, 1000.0f, 0.8f, 1200.0f};
    i.sampler.cutoff_hz = 1400.0f;
    i.sampler.resonance = 0.15f;
    i.sampler.env_amount = 0.5f;
    i.sampler.filter_env = Adsr{600.0f, 1500.0f, 0.5f, 1200.0f};
    i.sampler.volume_db = -8.0f;
    return i;
}

Instrument synth_bass(const char* name, float cutoff_hz, float env) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Saw;
    i.synth.osc2 = OscWave::Off;
    i.synth.sub_level = 0.5f;
    i.sampler.amp = Adsr{2.0f, 180.0f, 0.6f, 80.0f};
    i.sampler.cutoff_hz = cutoff_hz;
    i.sampler.resonance = 0.3f;
    i.sampler.env_amount = env;
    i.sampler.filter_env = Adsr{1.0f, 150.0f, 0.0f, 100.0f};
    i.sampler.key_track = 0.3f;
    i.sampler.volume_db = -3.0f;
    return i;
}

Instrument bell(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Sine;
    i.synth.osc2 = OscWave::Sine;
    i.synth.osc2_semitones = 19; // a twelfth above: bell-like partial
    i.synth.osc2_detune = 3.0f;
    i.synth.osc2_level = 0.45f;
    i.sampler.amp = Adsr{1.0f, 1400.0f, 0.0f, 900.0f};
    i.sampler.filter = FilterType::Off;
    i.sampler.volume_db = -5.0f;
    return i;
}

Instrument organ(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Sine;
    i.synth.osc2 = OscWave::Sine;
    i.synth.osc2_semitones = 12;
    i.synth.osc2_level = 0.5f;
    i.synth.sub_level = 0.4f;
    i.synth.lfo_rate_hz = 6.0f;
    i.synth.lfo_to_amp = 0.12f;
    i.sampler.amp = Adsr{5.0f, 100.0f, 1.0f, 60.0f};
    i.sampler.filter = FilterType::Off;
    i.sampler.volume_db = -8.0f;
    return i;
}

Instrument pluck(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Saw;
    i.synth.osc2 = OscWave::Pulse;
    i.synth.osc2_level = 0.4f;
    i.sampler.amp = Adsr{1.0f, 450.0f, 0.15f, 200.0f};
    i.sampler.cutoff_hz = 900.0f;
    i.sampler.env_amount = 3.0f;
    i.sampler.filter_env = Adsr{1.0f, 180.0f, 0.0f, 150.0f};
    i.sampler.volume_db = -6.0f;
    return i;
}

Instrument brass(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Saw;
    i.synth.osc2 = OscWave::Saw;
    i.synth.osc2_detune = 8.0f;
    i.synth.unison = 2;
    i.sampler.amp = Adsr{25.0f, 300.0f, 0.8f, 180.0f};
    i.sampler.cutoff_hz = 800.0f;
    i.sampler.resonance = 0.1f;
    i.sampler.env_amount = 2.2f;
    i.sampler.filter_env = Adsr{60.0f, 400.0f, 0.4f, 200.0f};
    i.sampler.volume_db = -8.0f;
    return i;
}

Instrument reed(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Pulse;
    i.synth.pulse_width = 0.3f;
    i.synth.osc2 = OscWave::Off;
    i.synth.noise_level = 0.05f; // breath
    i.synth.lfo_rate_hz = 5.0f;
    i.synth.lfo_to_pitch = 8.0f;
    i.sampler.amp = Adsr{40.0f, 200.0f, 0.85f, 150.0f};
    i.sampler.cutoff_hz = 1800.0f;
    i.sampler.env_amount = 0.8f;
    i.sampler.volume_db = -7.0f;
    return i;
}

Instrument lead(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Saw;
    i.synth.osc2 = OscWave::Pulse;
    i.synth.osc2_detune = 10.0f;
    i.synth.glide_ms = 40.0f;
    i.synth.lfo_rate_hz = 5.5f;
    i.synth.lfo_to_pitch = 10.0f;
    i.sampler.amp = Adsr{3.0f, 300.0f, 0.75f, 150.0f};
    i.sampler.cutoff_hz = 2500.0f;
    i.sampler.env_amount = 1.5f;
    i.sampler.volume_db = -8.0f;
    return i;
}

Instrument sfx(const char* name) {
    Instrument i = make_instrument(InstrumentKind::Synth, name);
    i.synth.osc1 = OscWave::Noise;
    i.synth.osc2 = OscWave::Saw;
    i.synth.osc2_level = 0.3f;
    i.synth.lfo_shape = LfoShape::SampleHold;
    i.synth.lfo_rate_hz = 12.0f;
    i.synth.lfo_to_cutoff = 2.0f;
    i.sampler.amp = Adsr{5.0f, 600.0f, 0.3f, 400.0f};
    i.sampler.cutoff_hz = 1500.0f;
    i.sampler.resonance = 0.6f;
    i.sampler.volume_db = -10.0f;
    return i;
}

Instrument drum_kit(const char* name) { return make_instrument(InstrumentKind::Drum, name); }

Instrument gb_pulse(const char* name, int duty, bool vibrato) {
    Instrument i = make_instrument(InstrumentKind::Gb, name);
    i.gb.duty = std::clamp(duty, 0, 3);
    i.gb.macros[kMacroDuty] = GbMacro{}; // a fixed duty: the shape is the point
    if (!vibrato) {
        i.gb.vibrato_cents = 0;
    }
    return i;
}

Instrument gb_wave(const char* name, int wave) {
    Instrument i = make_instrument(InstrumentKind::Gb, name);
    i.gb = default_gb(GbChannel::Wave);
    // Wave RAM shape 0 plays unless a wave macro picks another: put the chosen shape there.
    i.gb.waves[0] = i.gb.waves[static_cast<std::size_t>(std::clamp(wave, 0, static_cast<int>(i.gb.waves.size()) - 1))];
    return i;
}

Instrument gb_noise(const char* name, bool seven_bit, int env_pace) {
    Instrument i = make_instrument(InstrumentKind::Gb, name);
    i.gb = default_gb(GbChannel::Noise);
    i.gb.noise_short = seven_bit;
    i.gb.env_pace = env_pace;
    return i;
}

Instrument to_snes(const Instrument& source, const char* name, std::uint32_t rate, bool sustain, double seconds,
                   int play_note, int root, int brr_rate) {
    Document tmp;
    tmp.instruments[0] = source;
    if (sustain) { // a static, periodic tone loops cleanly over a few cycles
        Instrument& s = tmp.instruments[0];
        s.sampler.amp = Adsr{3.0f, 30.0f, 1.0f, 30.0f};
        s.sampler.env_amount = 0.0f;
        s.synth.unison = 1;
        s.synth.osc2_detune = 0.0f;
        s.synth.lfo_to_pitch = s.synth.lfo_to_cutoff = s.synth.lfo_to_pw = s.synth.lfo_to_amp = 0.0f;
        s.synth.noise_level = 0.0f;
        s.synth.glide_ms = 0.0f;
    }
    Pattern p;
    p.steps = 16;
    // 120 BPM: 2 beats = 1 s. Sustained sounds are held 2 s; one-shots are released at once.
    note(p, 0, 0.0, sustain ? 4.0 : 0.25, sustain ? 69 : play_note, 110);
    tmp.song.patterns = {p};
    tmp.song.sections[0].bpm10 = 1200;
    engine::OfflineRender opt;
    opt.master = false;
    opt.sample_rate = rate;
    opt.name = name;
    opt.tail_s = sustain ? 0.0 : std::max(0.0, seconds - 2.0);
    Instrument i = make_instrument(InstrumentKind::Snes, name);
    const auto full = engine::render_section(tmp, opt);
    // Mono (the S-DSP voice is mono; SNES pan is per voice) and, for one-shots, `seconds` long.
    // Sustained: 0.1 s in (settled), a loop of 22 cycles of 440 Hz (2 x rate / 40 frames); the
    // sample ends with the loop.
    const auto loop_start = static_cast<std::size_t>(std::llround(4.0 * rate / 40.0));
    const auto loop_end = static_cast<std::size_t>(std::llround(6.0 * rate / 40.0));
    const auto frames = std::min(full->frames, sustain ? loop_end : static_cast<std::size_t>(seconds * rate));
    std::vector<float> mono(frames);
    double mean = 0.0;
    for (std::size_t f = 0; f < frames; ++f) {
        mono[f] = full->channels == 2 ? 0.5f * (full->left[f] + full->right[f]) : full->left[f];
        mean += mono[f];
    }
    // Library-ready: no DC, peak at -1 dBFS (instruments sit at the same level before mixing).
    mean /= static_cast<double>(std::max<std::size_t>(1, frames));
    float peak = 0.0f;
    for (float& x : mono) {
        x -= static_cast<float>(mean);
        peak = std::max(peak, std::fabs(x));
    }
    if (peak > 1e-6f) {
        for (float& x : mono) {
            x *= 0.891f / peak;
        }
    }
    i.sample = make_sample(name, "", rate, std::move(mono));
    i.sampler.root_note = sustain ? 69 : root;
    i.snes.root_note = i.sampler.root_note;
    if (sustain) {
        i.sampler.loop = LoopMode::Forward;
        i.sampler.loop_start = loop_start;
        i.sampler.loop_end = loop_end;
    }
    i.snes.source_rate = brr_rate;
    i.brr = io::encode_for_snes(*i.sample, i.sampler, i.snes);
    return i;
}

Instrument snes_pad(std::uint32_t rate) {
    Instrument src = make_instrument(InstrumentKind::Synth, "PAD SOURCE");
    src.synth.osc2 = OscWave::Off;
    src.sampler.amp = Adsr{10.0f, 100.0f, 1.0f, 100.0f};
    src.sampler.cutoff_hz = 2000.0f;
    src.sampler.env_amount = 0.0f;
    src.sampler.volume_db = -6.0f;
    Instrument i = to_snes(src, "SNES PAD", rate, true);
    i.snes.attack = 9;
    i.snes.sustain_level = 6;
    i.snes.sustain_rate = 10;
    return i;
}

} // namespace phos::content::presets
