#include "engine/gb_voice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::engine {

namespace {

// Duty patterns, 8 steps per cycle (Pan Docs "Audio Registers", AUD1LEN duty).
constexpr std::uint8_t kDuty[4] = {0b00000001, 0b10000001, 0b10000111, 0b01111110};
constexpr int kWaveShift[4] = {4, 0, 1, 2}; // output level: mute, 100 %, 50 %, 25 %

double note_hz(double semis) { return 440.0 * std::exp2((semis - 69.0) / 12.0); }

} // namespace

int GbVoice::macro_value(const model::GbParams& p, int kind, int fallback) const noexcept {
    const auto& m = p.macros[static_cast<std::size_t>(kind)];
    if (m.steps.empty()) {
        return fallback;
    }
    return m.steps[static_cast<std::size_t>(std::clamp(macro_pos_[static_cast<std::size_t>(kind)], 0,
                                                       static_cast<int>(m.steps.size()) - 1))];
}

void GbVoice::start(const model::GbParams& p, int note, int from_note, float rate) {
    active_ = true;
    released_ = false;
    release_pending_ = false;
    stopped_ = false;
    channel_ = p.channel;
    rate_ = rate;
    note_ = note;
    porta_from_ = p.porta_ticks > 0 && from_note >= 0 ? from_note : -1.0;
    porta_ticks_left_ = p.porta_ticks;
    phase_ = 0.0;
    noise_acc_ = 0.0;
    lfsr_ = 0; // Pan Docs: the LFSR is set to 0 on (re)trigger
    volume_ = std::clamp(p.env_volume, 0, 15);
    env_timer_ = 0;
    sweep_timer_ = 0;
    sweep_period_ = -1;
    length_left_ = p.length > 0 ? p.length : 0;
    seq_acc_ = 0.0;
    seq_step_ = 0;
    tick_acc_ = 0.0;
    ticks_ = 0;
    macro_pos_.fill(0);
    last_digital_ = 0;
    last_analog_ = 0.0f;
    hp_cap_ = 0.0f;
    hp_factor_ = static_cast<float>(std::pow(0.999958, 4194304.0 / rate)); // Pan Docs "Audio Details"
    blep_.reset();
    silent_frames_ = 0;
    tick60(p); // apply the first macro step immediately
}

void GbVoice::apply_release(const model::GbParams& p) {
    release_pending_ = false;
    released_ = true;
    bool has_release = false;
    for (int k = 0; k < model::kMacroCount; ++k) {
        const auto& m = p.macros[static_cast<std::size_t>(k)];
        if (m.release >= 0 && m.release < static_cast<int>(m.steps.size())) {
            macro_pos_[static_cast<std::size_t>(k)] = m.release;
            has_release = true;
        }
    }
    if (!has_release) {
        stopped_ = true; // plain key-off: the channel stops (the step is band-limited, no click)
    }
}

void GbVoice::tick60(const model::GbParams& p) noexcept {
    // Macro stepping. Held: steps up to the release point and loops within that. Released: plays
    // from the release point to the end, looping only if the loop lies after the release point; a
    // release part that has run out ends the note (else a looping macro would keep it sounding).
    bool release_running = false;
    for (int k = 0; k < model::kMacroCount; ++k) {
        const auto& m = p.macros[static_cast<std::size_t>(k)];
        int& pos = macro_pos_[static_cast<std::size_t>(k)];
        if (m.steps.empty() || ticks_ == 0) {
            continue; // first tick shows step 0
        }
        const int n = static_cast<int>(m.steps.size());
        const bool has_release = m.release >= 0 && m.release < n;
        const int hold_end = !released_ && has_release ? m.release : n;
        const bool loop_ok = m.loop >= 0 && m.loop < hold_end && (!released_ || (has_release && m.loop >= m.release));
        if (pos + 1 < hold_end) {
            ++pos;
            release_running = release_running || (released_ && has_release);
        } else if (loop_ok) {
            pos = m.loop;
            release_running = release_running || released_;
        } // else: stay on the last step
    }
    if (released_ && ticks_ > 0 && !release_running) {
        stopped_ = true;
    }
    if (!p.macros[model::kMacroVolume].steps.empty()) {
        volume_ = std::clamp(macro_value(p, model::kMacroVolume, 15), 0, 15);
    }
    if (porta_ticks_left_ > 0) {
        --porta_ticks_left_;
    }
    ++ticks_;
}

void GbVoice::tick_frame_seq(const model::GbParams& p, int step) noexcept {
    // Length at 256 Hz (even steps), sweep at 128 Hz (steps 2, 6), envelope at 64 Hz (step 7).
    if (step % 2 == 0 && length_left_ > 0 && --length_left_ == 0) {
        stopped_ = true; // length timer expired: the channel turns off
        released_ = true;
    }
    if ((step == 2 || step == 6) && channel_ == model::GbChannel::Pulse && p.sweep_pace > 0) {
        if (++sweep_timer_ >= p.sweep_pace) {
            sweep_timer_ = 0;
            if (sweep_period_ < 0) {
                sweep_period_ = model::gb_pulse_period(note_hz(note_));
            }
            const int delta = sweep_period_ >> p.sweep_shift; // Lt+1 = Lt +- Lt / 2^step
            sweep_period_ += p.sweep_down ? -delta : delta;
            if (sweep_period_ > 2047) {
                stopped_ = true; // overflow turns the channel off
                released_ = true;
                sweep_period_ = 2047;
            }
            sweep_period_ = std::max(0, sweep_period_);
        }
    }
    if (step == 7 && p.env_pace > 0 && p.macros[model::kMacroVolume].steps.empty()) {
        if (++env_timer_ >= p.env_pace) {
            env_timer_ = 0;
            volume_ = std::clamp(volume_ + (p.env_up ? 1 : -1), 0, 15);
        }
    }
}

double GbVoice::target_hz(const model::GbParams& p, double pitch) const noexcept {
    double semis = note_ + macro_value(p, model::kMacroArp, 0) + macro_value(p, model::kMacroPitch, 0) / 100.0;
    if (porta_from_ >= 0.0 && p.porta_ticks > 0) {
        const double t = 1.0 - static_cast<double>(porta_ticks_left_) / p.porta_ticks;
        semis = porta_from_ + (semis - porta_from_) * t;
    }
    if (p.vibrato_cents > 0 && ticks_ > p.vibrato_delay) {
        const double cycle = std::max(1, p.vibrato_speed) * 4.0;
        semis += p.vibrato_cents / 100.0 * std::sin(2.0 * std::numbers::pi * (ticks_ - p.vibrato_delay) / cycle);
    }
    double hz = note_hz(semis) * pitch;
    if (channel_ == model::GbChannel::Pulse) {
        if (sweep_period_ >= 0) {
            return model::gb_pulse_hz(sweep_period_);
        }
        if (p.quantize) {
            hz = model::gb_pulse_hz(model::gb_pulse_period(hz));
        }
    } else if (channel_ == model::GbChannel::Wave && p.quantize) {
        hz = model::gb_wave_hz(model::gb_wave_period(hz));
    }
    return hz;
}

int GbVoice::digital(const model::GbParams& p) const noexcept {
    switch (channel_) {
    case model::GbChannel::Pulse: {
        if (stopped_) {
            return 0;
        }
        const int duty = std::clamp(macro_value(p, model::kMacroDuty, p.duty), 0, 3);
        const int step = static_cast<int>(phase_ * 8.0) & 7;
        return (kDuty[duty] >> (7 - step)) & 1 ? volume_ : 0;
    }
    case model::GbChannel::Wave: {
        if (volume_ == 0 || stopped_) {
            return 0; // key-off / length expiry gate the wave channel too
        }
        const int w = std::clamp(macro_value(p, model::kMacroWave, 0), 0, 7);
        const int idx = static_cast<int>(phase_ * 32.0) & 31;
        const int v = p.waves[static_cast<std::size_t>(w)][static_cast<std::size_t>(idx)] & 0x0F;
        return v >> kWaveShift[std::clamp(p.wave_level, 0, 3)];
    }
    case model::GbChannel::Noise:
        return !stopped_ && (lfsr_ & 1u) ? volume_ : 0; // Pan Docs: bit 0 selects between 0 and the volume
    }
    return 0;
}

void GbVoice::render(float* left, float* right, std::size_t frames, const model::GbParams& p, double pitch, float gain,
                     float pl, float pr) noexcept {
    if (!active_) {
        return;
    }
    if (release_pending_) {
        apply_release(p);
    }
    const double tick_inc = 60.0 / rate_;
    const double seq_inc = 512.0 / rate_;
    hz_ = target_hz(p, pitch);
    for (std::size_t i = 0; i < frames; ++i) {
        // Driver tick and frame sequencer at sample resolution (their steps are band-limited too).
        tick_acc_ += tick_inc;
        if (tick_acc_ >= 1.0) {
            tick_acc_ -= 1.0;
            tick60(p);
            hz_ = target_hz(p, pitch);
        }
        seq_acc_ += seq_inc;
        if (seq_acc_ >= 1.0) {
            seq_acc_ -= 1.0;
            tick_frame_seq(p, seq_step_);
            seq_step_ = (seq_step_ + 1) & 7;
            hz_ = target_hz(p, pitch); // a sweep step changes the period
        }
        const double hz = hz_;
        // Advance through this sample, adding a step at every change of the digital output.
        auto emit = [&](float frac) {
            const int d = digital(p);
            if (d != last_digital_) {
                // DAC: digital 0..15 to analog (Pan Docs "Audio Details"); 0 at digital 0 here so a
                // silent channel is silent (the high-pass would remove the offset anyway).
                const float analog = static_cast<float>(d) / 15.0f;
                blep_.add_step(frac, analog - last_analog_, p.raw);
                last_analog_ = analog;
                last_digital_ = d;
            }
        };
        emit(0.0f); // envelope / macro / duty changes since the last sample
        if (channel_ == model::GbChannel::Noise) {
            // Note -> the nearest LFSR clock (divider/shift grid), clocked 16x the note frequency.
            const double target = hz * 16.0;
            if (target != noise_target_) {
                noise_target_ = target;
                double best = model::gb_noise_hz(0, 0);
                for (int s = 0; s < 14; ++s) {
                    for (int d = 0; d < 8; ++d) {
                        const double f = model::gb_noise_hz(d, s);
                        if (std::fabs(std::log(f / target)) < std::fabs(std::log(best / target))) {
                            best = f;
                        }
                    }
                }
                noise_clock_ = best;
            }
            const double per_sample = noise_clock_ / rate_;
            noise_acc_ += per_sample;
            int guard = 0;
            while (noise_acc_ >= 1.0 && guard++ < 16) { // bounded: at most 16 clocks per sample
                noise_acc_ -= 1.0;
                lfsr_ = model::gb_lfsr_step(lfsr_, p.noise_short);
                emit(static_cast<float>(1.0 - noise_acc_ / per_sample));
            }
        } else {
            const double steps = channel_ == model::GbChannel::Pulse ? 8.0 : 32.0;
            const double inc = hz / rate_;
            const double start = phase_;
            phase_ += inc;
            // Every step boundary crossed in this sample: duty edges / wave samples.
            const double first = std::floor(start * steps) + 1.0;
            for (double b = first; b <= phase_ * steps && b - first < 64.0; b += 1.0) {
                const double t = (b / steps - start) / inc; // 0..1 within the sample
                const double saved = phase_;
                phase_ = b / steps;
                emit(static_cast<float>(t));
                phase_ = saved;
            }
            if (phase_ >= 1.0) {
                phase_ -= std::floor(phase_);
            }
        }
        // DMG high-pass (capacitor), then out.
        const float in = blep_.read();
        const float out = in - hp_cap_;
        hp_cap_ = in - out * hp_factor_;
        const float s = out * gain * 0.5f;
        left[i] += s * pl;
        right[i] += s * pr;
        silent_frames_ = volume_ == 0 || stopped_ ? silent_frames_ + 1 : 0;
        if ((released_ || stopped_) && silent_frames_ > 64) { // the band-limited tail has passed
            active_ = false;
            return;
        }
    }
}

} // namespace phos::engine
