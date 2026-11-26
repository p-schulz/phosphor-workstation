#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

#include "model/workspace.h"

// Ready-made instruments and pattern helpers for the templates, the example projects and the
// MIDI import (no third-party audio: everything is synthesised or rendered offline).
namespace phos::content::presets {

// Pattern editing in beats (quarter notes).
void note(model::Pattern& p, int track, double beat, double beats, int midi, int velocity);
void chord(model::Pattern& p, int track, double beat, double beats, std::initializer_list<int> notes, int velocity);

model::Instrument electric_piano();
model::Instrument pad(const char* name);
model::Instrument synth_bass(const char* name, float cutoff_hz, float env);
model::Instrument bell(const char* name);
model::Instrument organ(const char* name);
model::Instrument pluck(const char* name);
model::Instrument brass(const char* name);
model::Instrument reed(const char* name);
model::Instrument lead(const char* name);
model::Instrument sfx(const char* name);
model::Instrument drum_kit(const char* name);

// Game Boy voices: a pulse lead (duty 0..3), a wave channel with one of the built-in shapes,
// a noise channel voice (short hit or long hiss).
model::Instrument gb_pulse(const char* name, int duty, bool vibrato = true);
model::Instrument gb_wave(const char* name, int wave);
model::Instrument gb_noise(const char* name, bool seven_bit, int env_pace);

// A SNES instrument made from `source` (any kind) and converted to BRR at `brr_rate`. `sustain`: a
// steady tone at A4 with a short loop of exactly 22 cycles (the source is made static first: no
// detune, LFO, filter sweep or noise; the SNES ADSR shapes it), so it fits the 64 KB ARAM like a
// console sample; else a one-shot of `seconds` played at `play_note` (drum pads: their note) that
// sounds at its own pitch on `root`. Mono, DC-free, normalised.
model::Instrument to_snes(const model::Instrument& source, const char* name, std::uint32_t rate, bool sustain,
                          double seconds = 1.0, int play_note = 69, int root = 69, int brr_rate = 32000);
model::Instrument snes_pad(std::uint32_t rate);

} // namespace phos::content::presets
