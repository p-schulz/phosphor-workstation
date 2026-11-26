#include "model/wavetable.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::model {

namespace wavetable {

namespace {

bool valid(const WavetableData& d, int segment) { return segment >= 0 && segment < static_cast<int>(d.segments.size()); }

} // namespace

WavetableData compute(const WavetableData& in, bool normalize) {
    WavetableData out = in;
    float peak = 0.0f;
    for (auto& seg : out.segments) {
        for (int i = 0; i < kPoints; ++i) {
            double v = 0.0;
            for (int h = 1; h <= kHarmonics; ++h) {
                const float a = seg.harmonics[static_cast<std::size_t>(h - 1)];
                if (a != 0.0f) {
                    v += a * std::sin(2.0 * std::numbers::pi * h * i / kPoints);
                }
            }
            seg.wave[static_cast<std::size_t>(i)] = static_cast<float>(v);
            peak = std::max(peak, std::fabs(static_cast<float>(v)));
        }
    }
    if (normalize && peak > 0.0f) {
        for (auto& seg : out.segments) {
            for (float& v : seg.wave) {
                v /= peak;
            }
        }
    }
    return out;
}

WavetableData resize(const WavetableData& in, int count) {
    WavetableData out = in;
    count = std::clamp(count, 1, WavetableData::kMaxSegments);
    const WaveSegment fill = in.segments.empty() ? WaveSegment{} : in.segments.back();
    out.segments.resize(static_cast<std::size_t>(count), fill);
    return out;
}

WavetableData set_points(const WavetableData& in, int segment, int from, int to, float from_value, float to_value) {
    WavetableData out = in;
    if (!valid(out, segment)) {
        return out;
    }
    if (from > to) {
        std::swap(from, to);
        std::swap(from_value, to_value);
    }
    from = std::clamp(from, 0, kPoints - 1);
    to = std::clamp(to, 0, kPoints - 1);
    auto& w = out.segments[static_cast<std::size_t>(segment)].wave;
    for (int i = from; i <= to; ++i) {
        const float t = to == from ? 1.0f : static_cast<float>(i - from) / static_cast<float>(to - from);
        w[static_cast<std::size_t>(i)] = std::clamp(from_value + (to_value - from_value) * t, -1.0f, 1.0f);
    }
    return out;
}

WavetableData smooth(const WavetableData& in, int segment) {
    WavetableData out = in;
    if (!valid(out, segment)) {
        return out;
    }
    const auto& src = in.segments[static_cast<std::size_t>(segment)].wave;
    auto& dst = out.segments[static_cast<std::size_t>(segment)].wave;
    for (int i = 0; i < kPoints; ++i) {
        const float prev = src[static_cast<std::size_t>((i + kPoints - 1) % kPoints)];
        const float next = src[static_cast<std::size_t>((i + 1) % kPoints)];
        dst[static_cast<std::size_t>(i)] = (prev + src[static_cast<std::size_t>(i)] + next) / 3.0f;
    }
    return out;
}

WavetableData copy_segment(const WavetableData& in, int from, int to) {
    WavetableData out = in;
    if (valid(out, from) && valid(out, to)) {
        out.segments[static_cast<std::size_t>(to)].wave = in.segments[static_cast<std::size_t>(from)].wave;
        out.segments[static_cast<std::size_t>(to)].harmonics = in.segments[static_cast<std::size_t>(from)].harmonics;
    }
    return out;
}

WavetableData interpolate(const WavetableData& in, int a, int b) {
    WavetableData out = in;
    if (!valid(out, a) || !valid(out, b)) {
        return out;
    }
    if (a > b) {
        std::swap(a, b);
    }
    const auto& sa = in.segments[static_cast<std::size_t>(a)];
    const auto& sb = in.segments[static_cast<std::size_t>(b)];
    for (int s = a + 1; s < b; ++s) {
        const float t = static_cast<float>(s - a) / static_cast<float>(b - a);
        auto& seg = out.segments[static_cast<std::size_t>(s)];
        for (std::size_t i = 0; i < seg.wave.size(); ++i) {
            seg.wave[i] = sa.wave[i] + (sb.wave[i] - sa.wave[i]) * t;
        }
        for (std::size_t h = 0; h < seg.harmonics.size(); ++h) {
            seg.harmonics[h] = sa.harmonics[h] + (sb.harmonics[h] - sa.harmonics[h]) * t;
        }
    }
    return out;
}

WavetableData set_harmonic(const WavetableData& in, int segment, int harmonic, float value) {
    WavetableData out = in;
    if (valid(out, segment) && harmonic >= 0 && harmonic < kHarmonics) {
        out.segments[static_cast<std::size_t>(segment)].harmonics[static_cast<std::size_t>(harmonic)] =
            std::clamp(value, 0.0f, 1.0f);
    }
    return out;
}

WavetableData default_table() {
    WavetableData d;
    d.segments.resize(16);
    for (int s = 0; s < 16; ++s) {
        auto& seg = d.segments[static_cast<std::size_t>(s)];
        seg.cycles = 6;
        for (int h = 1; h <= kHarmonics; ++h) {
            // Saw-like 1/h spectrum; higher harmonics decay faster over the segments.
            const double decay = std::exp(-static_cast<double>(s) * h / 40.0);
            seg.harmonics[static_cast<std::size_t>(h - 1)] = static_cast<float>(decay / h);
        }
    }
    return compute(d, true);
}

} // namespace wavetable

const char* kind_name(InstrumentKind k) {
    switch (k) {
    case InstrumentKind::Sampler:
        return "SAMPLER";
    case InstrumentKind::Wavetable:
        return "WAVE";
    case InstrumentKind::Synth:
        return "SYNTH";
    case InstrumentKind::Drum:
        return "DRUM";
    case InstrumentKind::Gb:
        return "GB";
    case InstrumentKind::Snes:
        return "SNES";
    }
    return "SAMPLER";
}

const char* drum_type_name(DrumType t) {
    switch (t) {
    case DrumType::Off:
        return "OFF";
    case DrumType::Kick:
        return "KICK";
    case DrumType::Snare:
        return "SNARE";
    case DrumType::Hat:
        return "HAT";
    case DrumType::Clap:
        return "CLAP";
    case DrumType::Chip:
        return "CHIP";
    case DrumType::Tom:
        return "TOM";
    case DrumType::Sample:
        return "SAMPLE";
    }
    return "OFF";
}

DrumKit default_kit() {
    DrumKit k{};
    auto pad = [&](int note, DrumType t, float tune, float decay, float tone, float level, float pan, int choke) {
        k[static_cast<std::size_t>(note - kDrumBaseNote)] = DrumPad{t, tune, decay, tone, level, pan, choke, -1};
    };
    pad(36, DrumType::Kick, 0.0f, 350.0f, 0.5f, 0.0f, 0.0f, 0);
    pad(37, DrumType::Chip, 12.0f, 60.0f, 0.7f, -6.0f, 0.2f, 0);
    pad(38, DrumType::Snare, 0.0f, 180.0f, 0.6f, -2.0f, 0.0f, 0);
    pad(39, DrumType::Clap, 0.0f, 220.0f, 0.5f, -3.0f, -0.1f, 0);
    pad(40, DrumType::Snare, 3.0f, 120.0f, 0.8f, -4.0f, 0.1f, 0);
    pad(41, DrumType::Tom, -5.0f, 300.0f, 0.3f, -3.0f, -0.4f, 0);
    pad(42, DrumType::Hat, 0.0f, 50.0f, 0.6f, -8.0f, 0.3f, 1);
    pad(43, DrumType::Tom, 0.0f, 260.0f, 0.3f, -3.0f, 0.0f, 0);
    pad(44, DrumType::Hat, 0.0f, 90.0f, 0.5f, -9.0f, 0.3f, 1);
    pad(45, DrumType::Tom, 5.0f, 220.0f, 0.3f, -3.0f, 0.4f, 0);
    pad(46, DrumType::Hat, 0.0f, 400.0f, 0.6f, -8.0f, 0.3f, 1);
    pad(47, DrumType::Chip, 0.0f, 120.0f, 0.3f, -6.0f, -0.2f, 0);
    pad(48, DrumType::Chip, 24.0f, 40.0f, 0.9f, -8.0f, 0.0f, 0);
    pad(49, DrumType::Hat, -5.0f, 900.0f, 0.4f, -10.0f, -0.3f, 0); // crash-ish
    return k;
}

Instrument make_instrument(InstrumentKind kind, std::string name) {
    Instrument inst;
    inst.name = std::move(name);
    inst.kind = kind;
    switch (kind) {
    case InstrumentKind::Wavetable:
        inst.wavetable = std::make_shared<const WavetableData>(wavetable::default_table());
        inst.wave.loop_start = 8;
        inst.wave.loop_length = 8;
        inst.sampler.amp = Adsr{5.0f, 400.0f, 0.7f, 300.0f};
        break;
    case InstrumentKind::Synth:
        inst.sampler.amp = Adsr{20.0f, 500.0f, 0.7f, 400.0f};
        inst.sampler.filter = FilterType::LowPass;
        inst.sampler.cutoff_hz = 1200.0f;
        inst.sampler.resonance = 0.3f;
        inst.sampler.env_amount = 2.5f;
        inst.sampler.filter_env = Adsr{5.0f, 600.0f, 0.2f, 300.0f};
        inst.sampler.key_track = 0.5f;
        break;
    case InstrumentKind::Drum:
        inst.drum = default_kit();
        inst.sampler.vel_to_amp = 0.7f;
        break;
    case InstrumentKind::Gb:
        inst.gb = default_gb(GbChannel::Pulse);
        inst.sampler.amp = Adsr{0.5f, 10.0f, 1.0f, 5.0f}; // the chip envelope and macros shape the sound
        inst.sampler.vel_to_amp = 0.0f;
        break;
    case InstrumentKind::Snes:
        inst.sampler.vel_to_amp = 0.5f;
        break;
    case InstrumentKind::Sampler:
        break;
    }
    return inst;
}

} // namespace phos::model
