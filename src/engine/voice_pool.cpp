#include "engine/voice_pool.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::engine {

namespace {

constexpr float kVoiceGain = 0.25f;       // headroom so a few voices do not hit the limiter
constexpr float kSilence = 1e-4f;         // -80 dB: release ends here
constexpr double kClickHz = 2000.0;       // chip-blip click: short decaying 2 kHz burst
constexpr float kClickDecayMs = 3.0f;
constexpr float kClickLengthMs = 20.0f;

} // namespace

double VoicePool::note_hz(int note) noexcept { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

void VoicePool::prepare(float sample_rate) noexcept {
    sample_rate_ = sample_rate;
    set_envelope(5.0f, 60.0f);
    kill_all();
}

void VoicePool::set_limit(int voices) noexcept { limit_ = std::clamp(voices, 1, kMaxVoices); }

void VoicePool::set_envelope(float attack_ms, float release_ms) noexcept {
    attack_step_ = 1.0f / std::max(1.0f, attack_ms * 0.001f * sample_rate_);
    // Exponential release reaching -80 dB after release_ms.
    release_coef_ = std::exp(std::log(kSilence) / std::max(1.0f, release_ms * 0.001f * sample_rate_));
}

int VoicePool::pick_slot() noexcept {
    int released = -1;
    int quietest = -1;
    for (int i = 0; i < limit_; ++i) {
        const Voice& v = voices_[static_cast<std::size_t>(i)];
        if (v.stage == Stage::Off) {
            return i;
        }
        if (v.stage == Stage::Release &&
            (released < 0 || v.order < voices_[static_cast<std::size_t>(released)].order)) {
            released = i;
        }
        if (quietest < 0) {
            quietest = i;
        } else {
            const Voice& q = voices_[static_cast<std::size_t>(quietest)];
            const float lv = level(v);
            const float lq = level(q);
            if (lv < lq || (lv == lq && v.order < q.order)) {
                quietest = i;
            }
        }
    }
    ++steals_;
    return released >= 0 ? released : quietest;
}

void VoicePool::note_on(TestWave wave, int note, float velocity, int bus) noexcept {
    Voice& v = voices_[static_cast<std::size_t>(pick_slot())];
    v.bus = static_cast<std::uint8_t>(std::clamp(bus, 0, kBusCount - 1));
    v.stage = Stage::Attack;
    v.wave = wave;
    v.note = note;
    v.order = next_order_++;
    v.phase = 0.0;
    // Clicks: note 1 = accent (higher), else the plain click.
    v.phase_inc = (wave == TestWave::Click ? (note == 1 ? kClickHz * 1.5 : kClickHz) : note_hz(note)) / sample_rate_;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.env = 0.0f;
    v.age = 0;
    v.sustained = false;
    v.kind = Kind::Test;
}

VoicePool::Voice& VoicePool::start_voice(Kind kind, int slot, int note, float velocity, int bus) noexcept {
    Voice& v = voices_[static_cast<std::size_t>(pick_slot())];
    v.bus = static_cast<std::uint8_t>(std::clamp(bus, 0, kBusCount - 1));
    v.stage = Stage::Attack;
    v.kind = kind;
    v.slot = slot;
    v.note = note;
    v.order = next_order_++;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.sustained = false;
    return v;
}

void VoicePool::note_on_instrument(int slot, std::span<const SamplerBlock> blocks, int note, float velocity,
                                   int bus) noexcept {
    if (slot < 0 || slot >= static_cast<int>(blocks.size())) {
        return;
    }
    const SamplerBlock& b = blocks[static_cast<std::size_t>(slot)];
    seed_ = seed_ * 1664525u + 1013904223u; // per-note noise seed
    switch (b.kind) {
    case model::InstrumentKind::Sampler:
        note_on_sampler(slot, b, note, velocity, bus);
        return;
    case model::InstrumentKind::Wavetable: {
        if (!b.tables) {
            return;
        }
        if (strict_cmi_) {
            limit_kind(Kind::Wave, 16);
        }
        Voice& v = start_voice(Kind::Wave, slot, note, velocity, bus);
        v.wv.start(b.tables, b.params, note, v.velocity, sample_rate_);
        return;
    }
    case model::InstrumentKind::Synth: {
        int& last = last_note_[static_cast<std::size_t>(slot)];
        if (b.synth.mono) {
            // Legato: a held voice of this instrument glides to the new note.
            for (Voice& v : voices_) {
                if (v.kind == Kind::Synth && v.slot == slot && (v.stage == Stage::Attack || v.stage == Stage::Sustain)) {
                    v.syn.legato(note);
                    v.note = note;
                    v.sustained = false;
                    last = note;
                    return;
                }
            }
        }
        Voice& v = start_voice(Kind::Synth, slot, note, velocity, bus);
        v.syn.start(b.params, b.synth, note, last > 0 ? last : -1, v.velocity, sample_rate_, seed_);
        last = note;
        return;
    }
    case model::InstrumentKind::Gb: {
        int ch = -1;
        if (strict_gb_) {
            ch = gb_channel_for(b.gb.channel); // mono per channel: the old note on it stops
        }
        int& last = last_note_[static_cast<std::size_t>(slot)];
        Voice& v = start_voice(Kind::Gb, slot, note, velocity, bus);
        v.gb_channel = ch;
        v.gb.start(b.gb, note, last > 0 ? last : -1, sample_rate_);
        last = note;
        return;
    }
    case model::InstrumentKind::Snes: {
        if (b.snes.preview_original && b.sample) { // A/B: the original sample through the sampler
            SamplerBlock orig = b;
            orig.params.root_note = b.snes.root_note;
            note_on_sampler(slot, orig, note, velocity, bus);
            return;
        }
        if (!b.brr) {
            return;
        }
        if (strict_snes_) {
            limit_kind(Kind::Snes, 8);
        }
        Voice& v = start_voice(Kind::Snes, slot, note, velocity, bus);
        v.snes.start(b.brr, b.snes, note, b.params.fine_cents, v.velocity);
        return;
    }
    case model::InstrumentKind::Drum: {
        const int pad_index = note - model::kDrumBaseNote;
        if (pad_index < 0 || pad_index >= model::kDrumPads) {
            return;
        }
        const model::DrumPad& pad = b.drum[static_cast<std::size_t>(pad_index)];
        if (pad.choke > 0) {
            for (Voice& v : voices_) {
                if (v.kind == Kind::Drum && v.slot == slot && v.stage != Stage::Off && v.dv.choke_group() == pad.choke) {
                    v.dv.choke();
                }
            }
        }
        if (pad.type == model::DrumType::Sample) {
            // The pad plays another instrument's sample at its root key (tuned by the pad).
            if (pad.sample_slot >= 0 && pad.sample_slot < static_cast<int>(blocks.size())) {
                const SamplerBlock& src = blocks[static_cast<std::size_t>(pad.sample_slot)];
                note_on_sampler(pad.sample_slot, src, src.params.root_note + static_cast<int>(pad.tune), velocity, bus);
            }
            return;
        }
        if (pad.type == model::DrumType::Off) {
            return;
        }
        Voice& v = start_voice(Kind::Drum, slot, note, velocity, bus);
        v.dv.start(pad, v.velocity, sample_rate_, seed_);
        return;
    }
    }
}

void VoicePool::note_on_sampler(int slot, const SamplerBlock& block, int note, float velocity, int bus) noexcept {
    if (!block.sample) {
        return;
    }
    Voice& v = start_voice(Kind::Sampler, slot, note, velocity, bus);
    v.sv.start(block.sample, slot, block.params, note, v.velocity, sample_rate_);
}

void VoicePool::limit_kind(Kind kind, int max) noexcept {
    for (int guard = 0; guard < kMaxVoices; ++guard) {
        int count = 0;
        Voice* oldest = nullptr;
        for (Voice& v : voices_) {
            if (v.stage != Stage::Off && v.kind == kind) {
                ++count;
                if (!oldest || v.order < oldest->order) {
                    oldest = &v;
                }
            }
        }
        if (count < max || !oldest) {
            return;
        }
        oldest->stage = Stage::Off;
        oldest->sv.kill();
        oldest->wv.kill();
        oldest->snes.kill();
        ++steals_;
    }
}

int VoicePool::gb_channel_for(model::GbChannel c) noexcept {
    auto owner = [&](int ch) -> Voice* {
        for (Voice& v : voices_) {
            if (v.stage != Stage::Off && v.kind == Kind::Gb && v.gb_channel == ch) {
                return &v;
            }
        }
        return nullptr;
    };
    int ch = c == model::GbChannel::Wave ? 2 : c == model::GbChannel::Noise ? 3 : 0;
    if (c == model::GbChannel::Pulse) {
        // Pulse 1 or 2: a free one, else the one whose note started first.
        Voice* a = owner(0);
        Voice* b = owner(1);
        ch = !a ? 0 : !b ? 1 : (a->order <= b->order ? 0 : 1);
    }
    if (Voice* v = owner(ch)) {
        v->stage = Stage::Off;
        v->gb.kill();
        ++steals_;
    }
    return ch;
}

int VoicePool::active_of(model::InstrumentKind kind) const noexcept {
    const Kind k = kind == model::InstrumentKind::Sampler   ? Kind::Sampler
                   : kind == model::InstrumentKind::Wavetable ? Kind::Wave
                   : kind == model::InstrumentKind::Synth     ? Kind::Synth
                   : kind == model::InstrumentKind::Drum      ? Kind::Drum
                   : kind == model::InstrumentKind::Gb        ? Kind::Gb
                                                               : Kind::Snes;
    int n = 0;
    for (const Voice& v : voices_) {
        n += v.stage != Stage::Off && v.kind == k ? 1 : 0;
    }
    return n;
}

void VoicePool::render_snes(float* dry_l, float* dry_r, float* wet_l, float* wet_r, std::size_t frames,
                            std::span<const SamplerBlock> blocks, const std::int16_t* noise,
                            std::span<const float> track_gain, std::span<const float> track_pan) noexcept {
    std::fill_n(dry_l, frames, 0.0f);
    std::fill_n(dry_r, frames, 0.0f);
    std::fill_n(wet_l, frames, 0.0f);
    std::fill_n(wet_r, frames, 0.0f);
    bool any = false;
    for (const Voice& v : voices_) {
        any = any || (v.stage != Stage::Off && v.kind == Kind::Snes);
    }
    if (!any) {
        return;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        int prev = 0; // pitch modulation source: the previous SNES voice (pool order)
        for (Voice& v : voices_) {
            if (v.stage == Stage::Off || v.kind != Kind::Snes) {
                continue;
            }
            const auto slot = static_cast<std::size_t>(v.slot);
            if (slot >= blocks.size() || !v.snes.active()) {
                v.stage = Stage::Off;
                continue;
            }
            const SamplerBlock& b = blocks[slot];
            float l = 0.0f;
            float r = 0.0f;
            prev = v.snes.sample(b.snes, pitch_ratio_, prev, noise[i], l, r);
            const float g = v.bus < track_gain.size() ? track_gain[v.bus] : 1.0f;
            const float pan = v.bus < track_pan.size() ? track_pan[v.bus] : 0.0f;
            l *= g * std::min(1.0f, 1.0f - pan);
            r *= g * std::min(1.0f, 1.0f + pan);
            dry_l[i] += l;
            dry_r[i] += r;
            if (b.snes.echo) {
                wet_l[i] += l;
                wet_r[i] += r;
            }
        }
    }
}

void VoicePool::release(Voice& v) noexcept {
    switch (v.kind) {
    case Kind::Sampler:
        v.sv.release();
        break;
    case Kind::Wave:
        v.wv.release();
        break;
    case Kind::Synth:
        v.syn.release();
        break;
    case Kind::Drum:
        return; // one-shot: note-off does not stop a hit
    case Kind::Gb:
        v.gb.release();
        break;
    case Kind::Snes:
        v.snes.release();
        break;
    case Kind::Test:
        break;
    }
    v.stage = Stage::Release;
}

void VoicePool::release_slot(int slot) noexcept {
    for (Voice& v : voices_) {
        const int s = v.kind == Kind::Sampler ? v.sv.slot() : v.slot;
        if (v.kind != Kind::Test && s == slot && (v.stage == Stage::Attack || v.stage == Stage::Sustain)) {
            release(v);
        }
    }
}

void VoicePool::note_off(int note, int bus, bool honour_sustain) noexcept {
    for (Voice& v : voices_) {
        const bool held = v.stage == Stage::Attack || v.stage == Stage::Sustain;
        if (v.note == note && (bus < 0 || v.bus == bus) && (v.kind != Kind::Test || v.wave == TestWave::Sine) && held) {
            if (sustain_ && honour_sustain) {
                v.sustained = true;
            } else {
                release(v);
            }
        }
    }
}

void VoicePool::set_sustain(bool down) noexcept {
    sustain_ = down;
    if (down) {
        return;
    }
    for (Voice& v : voices_) {
        if (v.sustained && (v.stage == Stage::Attack || v.stage == Stage::Sustain)) {
            release(v);
        }
        v.sustained = false;
    }
}

void VoicePool::release_all() noexcept {
    for (Voice& v : voices_) {
        if (v.stage == Stage::Attack || v.stage == Stage::Sustain) {
            release(v);
        }
    }
}

void VoicePool::kill_all() noexcept {
    for (Voice& v : voices_) {
        v.stage = Stage::Off;
        v.sv.kill();
        v.wv.kill();
        v.syn.kill();
        v.dv.kill();
        v.gb.kill();
        v.snes.kill();
    }
}

int VoicePool::active() const noexcept {
    int n = 0;
    for (const Voice& v : voices_) {
        n += v.stage != Stage::Off ? 1 : 0;
    }
    return n;
}

void VoicePool::render_voice(Voice& v, float* left, float* right, std::size_t frames, double pitch) noexcept {
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    if (v.wave == TestWave::Click) {
        const float decay = std::exp(-1.0f / (kClickDecayMs * 0.001f * sample_rate_));
        const auto length = static_cast<std::uint32_t>(kClickLengthMs * 0.001f * sample_rate_);
        if (v.age == 0) {
            v.env = 1.0f;
        }
        for (std::size_t i = 0; i < frames && v.age < length; ++i, ++v.age) {
            const float s = static_cast<float>(std::sin(kTwoPi * v.phase)) * v.env * v.velocity * kVoiceGain * 2.0f;
            v.phase += v.phase_inc;
            v.phase -= std::floor(v.phase);
            v.env *= decay;
            left[i] += s;
            right[i] += s;
        }
        if (v.age >= length) {
            v.stage = Stage::Off;
        }
        return;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        switch (v.stage) {
        case Stage::Attack:
            v.env += attack_step_;
            if (v.env >= 1.0f) {
                v.env = 1.0f;
                v.stage = Stage::Sustain;
            }
            break;
        case Stage::Release:
            v.env *= release_coef_;
            if (v.env < kSilence) {
                v.stage = Stage::Off;
                return;
            }
            break;
        default:
            break;
        }
        const float s = static_cast<float>(std::sin(kTwoPi * v.phase)) * v.env * v.velocity * kVoiceGain;
        v.phase += v.phase_inc * pitch;
        if (v.phase >= 1.0) {
            v.phase -= std::floor(v.phase); // a bent step can exceed one cycle
        }
        left[i] += s;
        right[i] += s;
    }
}

void VoicePool::render(float* left, float* right, std::size_t frames, std::span<const SamplerBlock> blocks) noexcept {
    std::array<float*, kBusCount> l{};
    std::array<float*, kBusCount> r{};
    l.fill(left);
    r.fill(right);
    render(l, r, frames, blocks);
}

void VoicePool::render(std::span<float* const> lefts, std::span<float* const> rights, std::size_t frames,
                       std::span<const SamplerBlock> blocks, double bpm) noexcept {
    // Pitch modulation is updated every 32 frames (0.7 ms at 48 kHz): smooth enough for vibrato
    // and bend without a pow() per sample.
    constexpr std::size_t kChunk = 32;
    constexpr double kVibratoHz = 5.5;
    constexpr double kVibratoCents = 50.0;
    for (std::size_t pos = 0; pos < frames; pos += kChunk) {
        const std::size_t n = std::min(kChunk, frames - pos);
        double pitch = pitch_ratio_;
        if (vibrato_depth_ > 0.0f) {
            const double cents = kVibratoCents * vibrato_depth_ * std::sin(2.0 * std::numbers::pi * lfo_phase_);
            pitch *= std::exp2(cents / 1200.0);
        }
        lfo_phase_ += kVibratoHz * static_cast<double>(n) / sample_rate_;
        lfo_phase_ -= std::floor(lfo_phase_);
        for (Voice& v : voices_) {
            if (v.stage == Stage::Off) {
                continue;
            }
            float* left = lefts[v.bus] + pos;
            float* right = rights[v.bus] + pos;
            if (v.kind == Kind::Test) {
                render_voice(v, left, right, n, pitch);
                continue;
            }
            const auto slot = static_cast<std::size_t>(v.kind == Kind::Sampler ? v.sv.slot() : v.slot);
            if (slot >= blocks.size()) {
                v.stage = Stage::Off; // no parameters for this slot any more
                continue;
            }
            const SamplerBlock& b = blocks[slot];
            bool alive = false;
            switch (v.kind) {
            case Kind::Sampler:
                v.sv.render(left, right, n, b.params, pitch);
                alive = v.sv.active();
                break;
            case Kind::Wave:
                v.wv.render(left, right, n, b.params, b.wave, pitch);
                alive = v.wv.active();
                break;
            case Kind::Synth:
                v.syn.render(left, right, n, b.params, b.synth, pitch, bpm);
                alive = v.syn.active();
                break;
            case Kind::Drum:
                v.dv.render(left, right, n);
                alive = v.dv.active();
                break;
            case Kind::Gb: {
                const auto& pr = b.params;
                const float gain = std::pow(10.0f, pr.volume_db / 20.0f) * (1.0f - pr.vel_to_amp + pr.vel_to_amp * v.velocity);
                const float theta = (std::clamp(pr.pan, -1.0f, 1.0f) + 1.0f) * 0.785398163f;
                v.gb.render(left, right, n, b.gb, pitch, gain, std::cos(theta) * 1.41421356f, std::sin(theta) * 1.41421356f);
                alive = v.gb.active();
                break;
            }
            case Kind::Snes:
                alive = v.snes.active(); // rendered by render_snes() at 32 kHz
                break;
            case Kind::Test:
                break;
            }
            if (!alive) {
                v.stage = Stage::Off;
            }
        }
    }
}

} // namespace phos::engine
