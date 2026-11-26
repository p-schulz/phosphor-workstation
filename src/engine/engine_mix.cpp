// The mixer on the audio thread (PLAN.md §6.5, §7.5, §7.8): voices and loops into the track buses,
// per track two inserts, fader, pan and sends; returns A / B; SNES bus; master inserts, VAPOR,
// master gain and the limiter.

#include <algorithm>
#include <cmath>

#include "dsp/fx.h"
#include "engine/atomic_max.h"
#include "engine/engine.h"
#include "seq/timing.h"

namespace phos::engine {

namespace {

// VAPOR 0..1 -> the macro's targets (PLAN.md §7.5: loop slowdown, LP cutoff, plate mix and decay,
// wow depth, hiss).
struct VaporMap {
    model::FxSlot plate;
    model::FxSlot lofi;
    model::FxSlot tape;
    float loop_speed = 1.0f;
};

VaporMap vapor_map(int amount) {
    const float v = std::clamp(amount, 0, model::kVaporMax) / static_cast<float>(model::kVaporMax);
    VaporMap m;
    m.plate.type = model::FxType::Plate;
    m.plate.p = {0.5f + 0.45f * v, 0.5f, 30.0f, 1.0f, 0.4f * v};
    m.lofi.type = model::FxType::Lofi;
    m.lofi.p = {20000.0f * std::pow(0.1f, v), 0.15f, 0.0f, 0.0f, 1.0f}; // 20 kHz .. 2 kHz
    m.tape.type = model::FxType::Tape;
    m.tape.p = {0.8f * v, 0.35f * v, 0.25f * v, 0.6f * v, 1.0f};
    m.loop_speed = 1.0f - 0.3f * v;
    return m;
}

} // namespace

std::uint64_t Engine::launch_frame(int slot, std::uint64_t now) const noexcept {
    const auto& song = current_->song;
    const auto& sec = song.sections[static_cast<std::size_t>(seq_.playing() ? seq_.section() : song.current_section)];
    switch (current_->loops[static_cast<std::size_t>(slot)].quantize) {
    case model::LaunchQuantize::Immediate:
        return now;
    case model::LaunchQuantize::Beat:
        return seq_.next_grid_frame(song, now, sample_rate_, static_cast<std::uint32_t>(seq::beat_ticks(sec.sig_den)));
    case model::LaunchQuantize::Bar:
        return seq_.next_grid_frame(song, now, sample_rate_,
                                    static_cast<std::uint32_t>(seq::bar_ticks(sec.sig_num, sec.sig_den)));
    }
    return now;
}

void Engine::loop_command(int slot, int action, std::uint64_t now) noexcept {
    if (slot < 0 || slot >= model::kLoopSlots) {
        return;
    }
    const bool on = action == 2 ? !loops_.active(slot) : action == 1;
    if (on) {
        loops_.launch(slot, launch_frame(slot, now));
    } else {
        loops_.stop(slot, launch_frame(slot, now));
    }
}

void Engine::run_fx(dsp::FxUnit* unit, const model::FxSlot& slot, float* l, float* r, std::uint32_t frames,
                    double bpm) noexcept {
    // A unit made for another type or rate (until the UI republishes) is bypassed.
    if (!unit || slot.type == model::FxType::Off || unit->type() != slot.type ||
        unit->sample_rate() != static_cast<float>(sample_rate_)) {
        return;
    }
    dsp::FxContext ctx{&slot, bpm, nullptr, nullptr};
    if (slot.type == model::FxType::Duck) {
        const auto src = static_cast<std::size_t>(std::clamp(static_cast<int>(slot.p[0]), 0, kTrackCount - 1));
        ctx.side_l = bus_l_[src];
        ctx.side_r = bus_r_[src];
    }
    unit->process(l, r, frames, ctx);
}

void Engine::mix_tracks(float* l, float* r, std::uint32_t frames, double bpm) noexcept {
    const auto& song = current_->song;
    const auto& rack = current_->fx;
    for (float* p : ret_) {
        std::fill_n(p, frames, 0.0f);
    }
    for (int t = 0; t < kTrackCount; ++t) {
        const auto ti = static_cast<std::size_t>(t);
        const auto& tb = song.tracks[ti];
        float* tl = bus_l_[ti];
        float* tr = bus_r_[ti];
        for (std::size_t k = 0; k < 2; ++k) {
            run_fx(rack.tracks[ti][k], tb.inserts[k], tl, tr, frames, bpm);
        }
        auto& g = track_gain_[ti];
        g.set_target(tb.gain * auto_level_[ti]); // CC 7 / 11 automation
        const float pan = std::isnan(auto_pan_[ti]) ? tb.pan : auto_pan_[ti];
        const float theta = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.785398163f;
        const float pl = std::cos(theta) * 1.41421356f;
        const float pr = std::sin(theta) * 1.41421356f;
        const bool sends = tb.send_a > 0.0f || tb.send_b > 0.0f;
        float peak = 0.0f;
        for (std::uint32_t i = 0; i < frames; ++i) {
            const float gain = g.next();
            const float a = tl[i] * gain * pl;
            const float b = tr[i] * gain * pr;
            l[i] += a;
            r[i] += b;
            peak = std::max({peak, std::fabs(a), std::fabs(b)});
            if (sends) {
                ret_[0][i] += a * tb.send_a;
                ret_[1][i] += b * tb.send_a;
                ret_[2][i] += a * tb.send_b;
                ret_[3][i] += b * tb.send_b;
            }
        }
        store_max(bus_peak_[ti], peak);
    }
    // Returns: all wet; their level is the return level.
    for (std::size_t k = 0; k < 2; ++k) {
        model::FxSlot slot = song.returns[k];
        slot.p[model::kFxParams - 1] = 1.0f;
        if (slot.type == model::FxType::Duck || slot.type == model::FxType::Off) {
            continue; // nothing to return
        }
        float* rl = ret_[2 * k];
        float* rr = ret_[2 * k + 1];
        run_fx(rack.returns[k], slot, rl, rr, frames, bpm);
        const float level = song.return_level[k];
        for (std::uint32_t i = 0; i < frames; ++i) {
            l[i] += rl[i] * level;
            r[i] += rr[i] * level;
        }
    }
}

void Engine::master_chain(float* l, float* r, std::uint32_t frames, double bpm) noexcept {
    const auto& song = current_->song;
    const auto& rack = current_->fx;
    for (std::size_t k = 0; k < 2; ++k) {
        run_fx(rack.master[k], song.master_fx[k], l, r, frames, bpm);
    }
    if (song.vapor > 0) {
        const VaporMap v = vapor_map(song.vapor);
        run_fx(rack.vapor[0], v.plate, l, r, frames, bpm);
        run_fx(rack.vapor[1], v.lofi, l, r, frames, bpm);
        run_fx(rack.vapor[2], v.tape, l, r, frames, bpm);
    }
}

void Engine::render_block(float* out, std::uint32_t frames, std::uint32_t channels, std::uint64_t frame) noexcept {
    for (int b = 0; b < kBusCount; ++b) {
        std::fill_n(bus_l_[static_cast<std::size_t>(b)], frames, 0.0f);
        std::fill_n(bus_r_[static_cast<std::size_t>(b)], frames, 0.0f);
    }
    const auto& song = current_->song;
    const int sec = seq_.playing() ? seq_.section() : song.current_section;
    const int bpm10 = song.sections[static_cast<std::size_t>(sec)].bpm10;
    const double bpm = bpm10 / 10.0;
    voices_.render(bus_l_, bus_r_, frames, current_->instruments, bpm);
    loops_.render(current_->loops, std::span<float* const>(bus_l_.data(), kTrackCount),
                  std::span<float* const>(bus_r_.data(), kTrackCount), frame, frames, bpm10,
                  song.vapor > 0 ? vapor_map(song.vapor).loop_speed : 1.0f);

    // The direct bus (metronome, audition) joins at unity; then the tracks with their effects.
    float* l = left_.data();
    float* r = right_.data();
    std::copy_n(bus_l_[kDirectBus], frames, l);
    std::copy_n(bus_r_[kDirectBus], frames, r);
    float direct_peak = 0.0f;
    for (std::uint32_t i = 0; i < frames; ++i) {
        direct_peak = std::max({direct_peak, std::fabs(l[i]), std::fabs(r[i])});
    }
    store_max(bus_peak_[kDirectBus], direct_peak);
    mix_tracks(l, r, frames, bpm);

    // SNES bus (PLAN.md §6.5): SNES voices at 32 kHz, echo, resampled into the master. Track gain
    // and pan apply per voice; track inserts and sends do not.
    std::array<float, kBusCount> gains{};
    std::array<float, kBusCount> pans{};
    for (int t = 0; t < kTrackCount; ++t) {
        const auto ti = static_cast<std::size_t>(t);
        gains[ti] = song.tracks[ti].gain * auto_level_[ti];
        pans[ti] = std::isnan(auto_pan_[ti]) ? song.tracks[ti].pan : auto_pan_[ti];
    }
    gains[kDirectBus] = 1.0f;
    const std::size_t n32 = snes_bus_.needed(frames);
    const std::int16_t* noise = snes_bus_.noise(n32, song.snes_echo.noise_rate);
    voices_.render_snes(snes_bus_.dry_l(), snes_bus_.dry_r(), snes_bus_.wet_l(), snes_bus_.wet_r(), n32,
                        current_->instruments, noise, gains, pans);
    snes_bus_.finish(n32, song.snes_echo, l, r, frames);

    master_chain(l, r, frames, bpm);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const float g = master_gain_.next();
        l[i] *= g;
        r[i] *= g;
    }
    limiter_.process(l, r, frames);
    store_min(min_gain_, limiter_.take_min_gain());

    float pl = 0.0f;
    float pr = 0.0f;
    for (std::uint32_t i = 0; i < frames; ++i) {
        pl = std::max(pl, std::fabs(l[i]));
        pr = std::max(pr, std::fabs(r[i]));
    }
    store_max(peak_l_, pl);
    store_max(peak_r_, pr);

    if (channels == 2) {
        for (std::uint32_t i = 0; i < frames; ++i) {
            out[2 * i] = l[i];
            out[2 * i + 1] = r[i];
        }
        return;
    }
    for (std::uint32_t i = 0; i < frames; ++i) {
        float* f = out + static_cast<std::size_t>(i) * channels;
        if (channels == 1) {
            f[0] = 0.5f * (l[i] + r[i]);
            continue;
        }
        f[0] = l[i];
        f[1] = r[i];
        std::fill_n(f + 2, channels - 2, 0.0f);
    }
}

} // namespace phos::engine
