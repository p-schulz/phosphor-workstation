<p align="center">
  <img src="docs/img/phosphor-testcard.jpg" alt="PHOSPHOR test card on the simulated CRT" width="640">
</p>

<h1 align="center">PHOSPHOR MACHINE</h1>

A retro MIDI workstation with a phosphor CRT interface: page-based, keyboard commands, mouse as a light
pen. It renders into a fixed 512 × 384 indexed canvas and upscales through a CRT pass, so it shows correctly
on a small 4:3 monitor (a e s t h e t i c).

## Build

Requirements: CMake ≥ 3.25, Ninja, a C++20 compiler (Apple clang on macOS 13+, MSVC 2022 on Windows, GCC 12+ on Linux), network
access on first configure (dependencies are fetched and pinned, see [docs/deps.md](docs/deps.md)). On macOS simply install ninja using homebrew. 

macOS:

    cmake --preset mac-debug && cmake --build --preset mac-debug
    ctest --preset mac-debug
    ./build/mac-debug/phosphor

Windows (from a "x64 Native Tools" prompt, or prefix each command with `tools\win\dev.cmd`):

    cmake --preset win-debug && cmake --build --preset win-debug
    ctest --preset win-debug
    build\win-debug\phosphor.exe

Linux / Raspberry Pi (64-bit; installs packages, builds, tests; details in [docs/linux.md](docs/linux.md)):

    tools/setup-linux.sh
    tools/run-linux.sh

## Use

Type commands on the COMMAND line (`HELP`, `P1`, `PS`, `THEME AMBER`, `TESTCARD`, `DISPLAY NEXT` …), use
F1–F12 for pages, or point and click with the mouse. Settings live in `~/Library/Application Support/Phosphor/`
or `%APPDATA%\Phosphor\` (`display_profiles.json`, optional `keys.cfg`; examples in `conf/`).

### Side keys

On a screen wider than 4:3 (a 16:9 panel, a MacBook screen, an iPad via Sidecar) the margins beside the
picture hold large touch / click keys: INDEX, PAGE ◄ / ►, BACK and ESC on the left, ▲ / ▼, + / − (they
repeat while held) and ENTER on the right. `SIDEKEYS OFF | AUTO | ON` (per monitor, Page S): AUTO shows
them when the margins are wide enough, ON shrinks the picture to make room. `DISPLAY ASPECT 16:9` reshapes
the window to try them.

![The side keys beside Page R on a 16:9 screen, COLOR theme](docs/img/phosphor-side-keys.jpg)

### Themes

`THEME GREEN | AMBER | GREY | VAPOR | CYAN | COLOR | LCD` (`THEME` alone lists them), per monitor on Page S.
LCD imitates a 1990s monochrome LCD (dark ink on a yellow-green panel); the CRT pass leaves out bloom and
the shadow mask for it.

| AMBER | GREY |
|---|---|
| ![Page 8 in the AMBER theme](docs/img/phosphor-theme-amber.jpg) | ![Page S in the GREY theme](docs/img/phosphor-theme-grey.jpg) |

| **VAPOR** | **CYAN** |
| ![Page L in the VAPOR theme](docs/img/phosphor-theme-vapor.jpg) | ![Page 4 in the CYAN theme](docs/img/phosphor-theme-cyan.jpg) |
| **LCD** | |
| ![Page C in the LCD theme](docs/img/phosphor-theme-lcd.jpg) | |


## Screens

Every screen of the running app with the CRT pass on, each with an example loaded (from
[examples/](examples/README.md) or the demo fixture). Left: the GREEN theme; right: COLOR. Regenerate
them with `tools/screenshots.sh` (it plays the examples quietly and uses `midi_probe` as a MIDI keyboard
and synth).

### Page 1 — Index

Every page with its name and soft key; click a line or type `P<id>`. The header shows tempo, signature and song position, the bottom line the project, CPU load and voices.

| GREEN | COLOR |
|---|---|
| ![Page 1 — Index, GREEN theme](docs/img/phosphor-page-1.jpg) | ![Page 1 — Index, COLOR theme](docs/img/phosphor-page-1-color.jpg) |

### Page 2 — Disk control

File browser for samples (WAV, AIFF, FLAC, MP3, OGG), MIDI songs, instruments (`.phinst`) and projects: audition, import, load. Here the example instrument library (`LIBRARY`).

| GREEN | COLOR |
|---|---|
| ![Page 2 — Disk control, GREEN theme](docs/img/phosphor-page-2.jpg) | ![Page 2 — Disk control, COLOR theme](docs/img/phosphor-page-2-color.jpg) |

### Page 3 — Keyboard control

MIDI inputs with channel filters, timing (IMMEDIATE / STABLE), velocity curve, bend range, KEYJAZZ, CC learn and a live latency report with a histogram.

| GREEN | COLOR |
|---|---|
| ![Page 3 — Keyboard control, GREEN theme](docs/img/phosphor-page-3.jpg) | ![Page 3 — Keyboard control, COLOR theme](docs/img/phosphor-page-3-color.jpg) |

### Page 3 — Keyboard control, OUT view

MIDI outputs with clock, MIDI OUT / THRU / offset / latency compensation, and where each of the 16 tracks plays (instrument, MIDI out or both, port, channel, program). `P3 OUT` or PAGE DOWN.

| GREEN | COLOR |
|---|---|
| ![Page 3 — Keyboard control, OUT view, GREEN theme](docs/img/phosphor-page-3-out.jpg) | ![Page 3 — Keyboard control, OUT view, COLOR theme](docs/img/phosphor-page-3-out-color.jpg) |

### Page 4 — Harmonic profiles

Additive synthesis for wavetables: 32 harmonic levels per segment, drawn with the light pen and turned into waveforms with `COMPUTE`. Shown: the morphing wavetable of WAVEFORMS.

| GREEN | COLOR |
|---|---|
| ![Page 4 — Harmonic profiles, GREEN theme](docs/img/phosphor-page-4.jpg) | ![Page 4 — Harmonic profiles, COLOR theme](docs/img/phosphor-page-4-color.jpg) |

### Page 5 — Synth

The subtractive synth: two oscillators, sub, noise, unison, LFO, glide. Shown: the supersaw of WAVEFORMS.

| GREEN | COLOR |
|---|---|
| ![Page 5 — Synth, GREEN theme](docs/img/phosphor-page-5.jpg) | ![Page 5 — Synth, COLOR theme](docs/img/phosphor-page-5-color.jpg) |

### Page 5 — Synth, drum kit

The same page for a DRUM instrument: 16 synthesised (or sampled) pads with tune, decay, tone, level, pan and choke groups. Shown: the kit of VAPOR PLAZA.

| GREEN | COLOR |
|---|---|
| ![Page 5 — Synth, drum kit, GREEN theme](docs/img/phosphor-page-5-drum.jpg) | ![Page 5 — Synth, drum kit, COLOR theme](docs/img/phosphor-page-5-drum-color.jpg) |

### Page 6 — Waveform drawing

Draw a wavetable segment freehand or with lines, smooth it, copy it or interpolate between segments.

| GREEN | COLOR |
|---|---|
| ![Page 6 — Waveform drawing, GREEN theme](docs/img/phosphor-page-6.jpg) | ![Page 6 — Waveform drawing, COLOR theme](docs/img/phosphor-page-6-color.jpg) |

### Page 7 — Control parameters

Every instrument's level, pan, tuning, amp and filter envelopes, filter and velocity response; root, interpolation and loop for samples.

| GREEN | COLOR |
|---|---|
| ![Page 7 — Control parameters, GREEN theme](docs/img/phosphor-page-7.jpg) | ![Page 7 — Control parameters, COLOR theme](docs/img/phosphor-page-7-color.jpg) |

### Page 8 — Sound sampling

The sample editor: trim, fades, normalise, reverse, loop points, crossfade loop, with undo.

| GREEN | COLOR |
|---|---|
| ![Page 8 — Sound sampling, GREEN theme](docs/img/phosphor-page-8.jpg) | ![Page 8 — Sound sampling, COLOR theme](docs/img/phosphor-page-8-color.jpg) |

### Page 0 — Spectral drawing

All segments' harmonics as one matrix: paint how the spectrum changes over time.

| GREEN | COLOR |
|---|---|
| ![Page 0 — Spectral drawing, GREEN theme](docs/img/phosphor-page-0.jpg) | ![Page 0 — Spectral drawing, COLOR theme](docs/img/phosphor-page-0-color.jpg) |

### Page C — Chip voices, Game Boy

A Game Boy channel (pulse, wave or noise): duty, envelope, sweep, length, 4-bit wave RAM, quantised pitch and the 60 Hz macros. Shown: the lead of GB ADVENTURE.

| GREEN | COLOR |
|---|---|
| ![Page C — Chip voices, Game Boy, GREEN theme](docs/img/phosphor-page-c-gb.jpg) | ![Page C — Chip voices, Game Boy, COLOR theme](docs/img/phosphor-page-c-gb-color.jpg) |

### Page C — Chip voices, SNES

A SNES voice: BRR encoding, ADSR / GAIN, pitch modulation, noise, the echo with its 8-tap FIR and the 64 KB ARAM budget. Shown: the flute of SNES FIELD.

| GREEN | COLOR |
|---|---|
| ![Page C — Chip voices, SNES, GREEN theme](docs/img/phosphor-page-c-snes.jpg) | ![Page C — Chip voices, SNES, COLOR theme](docs/img/phosphor-page-c-snes-color.jpg) |

### Page D — Waveform display

The sound as a 3D waterfall of 128 slices with hidden lines. A wavetable is filled in between its segments the way it plays (the real segments drawn brighter); a sample is cut into 128 time slices, one pitch cycle each (CYCLE) or the slice itself (SPAN). The COLOR theme draws it violet to cyan.

| GREEN | COLOR |
|---|---|
| ![Page D — Waveform display, GREEN theme](docs/img/phosphor-page-d.jpg) | ![Page D — Waveform display, COLOR theme](docs/img/phosphor-page-d-color.jpg) |

| Sample, CYCLE (GREEN) | Sample, CYCLE (COLOR) |
|---|---|
| ![Page D — a sample in CYCLE view, GREEN theme](docs/img/phosphor-page-d-sample.jpg) | ![Page D — a sample in CYCLE view, COLOR theme](docs/img/phosphor-page-d-sample-color.jpg) |

### Page K — Virtual keyboard

KEYJAZZ: the computer keyboard as a two-octave piano, and every note coming in over MIDI (here C4 from a test source).

| GREEN | COLOR |
|---|---|
| ![Page K — Virtual keyboard, GREEN theme](docs/img/phosphor-page-k.jpg) | ![Page K — Virtual keyboard, COLOR theme](docs/img/phosphor-page-k-color.jpg) |

### Page L — Loop library

Eight loop slots that follow the tempo (VARISPEED, STRETCH or SLICE) with launch quantise, the slot's waveform and the VAPOR macro. Shown: VAPOR PLAZA playing.

| GREEN | COLOR |
|---|---|
| ![Page L — Loop library, GREEN theme](docs/img/phosphor-page-l.jpg) | ![Page L — Loop library, COLOR theme](docs/img/phosphor-page-l-color.jpg) |

### Page M — Mixer

Sixteen tracks with meters, faders, pan, mute / solo, two inserts and two sends each; master inserts and the returns; the panel edits the focused effect.

| GREEN | COLOR |
|---|---|
| ![Page M — Mixer, GREEN theme](docs/img/phosphor-page-m.jpg) | ![Page M — Mixer, COLOR theme](docs/img/phosphor-page-m-color.jpg) |

### Page R — Real-time composer

The pattern grid of 16 tracks, sections with tempo and signature, the song order and the transport. FOLLOW keeps the playhead in view; `PR AUTO` draws CC automation. Shown: GB ADVENTURE playing.

| GREEN | COLOR |
|---|---|
| ![Page R — Real-time composer, GREEN theme](docs/img/phosphor-page-r.jpg) | ![Page R — Real-time composer, COLOR theme](docs/img/phosphor-page-r-color.jpg) |

### Page S — Setup

Display profile per monitor (scale, side keys, panel, overscan, CRT pass, theme), the page key, and the audio device, rate and buffer.

| GREEN | COLOR |
|---|---|
| ![Page S — Setup, GREEN theme](docs/img/phosphor-page-s.jpg) | ![Page S — Setup, COLOR theme](docs/img/phosphor-page-s-color.jpg) |

### Page H — Help

Commands and keys for the current page.

| GREEN | COLOR |
|---|---|
| ![Page H — Help, GREEN theme](docs/img/phosphor-page-h.jpg) | ![Page H — Help, COLOR theme](docs/img/phosphor-page-h-color.jpg) |
