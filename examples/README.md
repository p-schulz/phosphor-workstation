# PHOSPHOR examples

Everything here is made by `tools/examplegen/examplegen.cpp` from PHOSPHOR's own synths, chips and
offline renderer; there is no third-party audio. Rebuild it with:

    cmake --build --preset mac-debug --target examples

## library/ — an instrument library (40 `.phinst` bundles)

| Group | Instruments |
|---|---|
| Game Boy | GB PULSE 12 / 25 / 50 / 75 (fixed duty), GB LEAD (volume + duty macros, vibrato), GB ARP (major-chord arp macro), GB WAVE TRI / SAW / ORGAN (4-bit wave RAM), GB NOISE KICK / SNARE / HAT |
| SNES | SNES STRINGS, BRASS, FLUTE, BASS (looped BRR), SNES HARP, KICK, SNARE, HAT (one-shot BRR): synth patches rendered at 32 kHz, DC removed, normalised, BRR-encoded |
| Vaporwave | E.PIANO, VAPOR PAD, SYNTH BASS, DRUM KIT, GLASS BELL |
| Shapes | SHAPE SINE, TRIANGLE, SAW, SQUARE, PULSE 25, PULSE 10, SUPERSAW, NOISE, WAVETABLE, SNES SAW (levels matched by RMS) |
| GM names | PIANO, BASS, STRINGS, SYNTH LEAD, DRUMS: picked up by `IMPORT song.mid` (see below) |

Use them: copy the bundles into your library folder (`~/Documents/PHOSPHOR/Instruments/`, the
folder `LIBRARY` opens on Page 2), select an instrument slot with `INST <n>` and type
`IMPORT GB ARP` (or press ENTER on it in Page 2). `EXPORT INST [name]` adds your own sounds.

## projects/ — five songs built from that library

Each project was assembled by loading instruments from `library/` (as `IMPORT` does) and naming
them after their role. `LOAD <path to the .phosphor>`, then Space.

| Project | What it shows |
|---|---|
| GB ADVENTURE | 140 BPM chiptune in A minor: intro (arpeggio macro), verse (pulse 25 % lead with delayed vibrato), chorus (lead + 12.5 % harmony), bridge; wave-channel bass, noise drums. SONG mode, order A B C B C D C. |
| SNES FIELD | 96 BPM RPG field theme in D: BRR strings, flute, brass, harp, bass and drums, S-DSP echo with the low-pass FIR, STRICT SNES (8 voices). |
| VAPOR PLAZA | 72 BPM: a 96 BPM e-piano / bass source (section D) resampled into two loop slots: VARISPEED (intro, outro) and SLICE (the groove, ducked by the kick); pad and bell on the plate, tape and chorus, VAPOR 50. |
| GATED DRUMS | 96 BPM: the same two bars of drums (big snare on 2 and 4, a tom fill) four times: dry, then through each mode of the GATE effect, the 1980s gated reverb: GATED (a room cut by a gate keyed from the drums), NONLIN (a flat burst that stops dead), REVERSE (a swelling burst). Each version has its own drum track with the effect as insert 1; bass and pad underneath. |
| WAVEFORMS | 16 shapes, one per track and section, each playing the same two bars (arpeggio, held note) at the same loudness: sine, triangle, saw, square, pulse 25 % / 10 %, supersaw, noise, Game Boy pulse 12.5 / 25 / 50 %, Game Boy 4-bit triangle / saw / organ waves, a morphing wavetable and a SNES BRR saw. Play the song to hear them in turn, or pick a section with `SECTION <A-P>`. |

## midi/ — GB ADVENTURE.mid

The GB song as a Standard MIDI file with General MIDI programs (square / saw lead, synth bass) and
drums on channel 10. With the library in place, `NEW` and `IMPORT <path>/GB ADVENTURE.mid` builds a
playable project: every track gets SYNTH LEAD, BASS or DRUMS from the library (by GM family), or a
built-in preset when the library has none.
