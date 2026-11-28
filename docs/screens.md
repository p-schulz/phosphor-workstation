# Screens

> **Status: APPROVED 2026-11-25** (reconstructed after the original was lost; decisions in §5).

Reference images are layout guidance only, kept locally in `docs/reference/` (not in the repository), never
packaged, never traced (PLAN.md §2). PHOSPHOR's own screenshots are `docs/img/phosphor-*.png`.

## 1. Geometry

| | Value | Why |
|---|---|---|
| Canvas | **512 × 384**, 8-bit indexed | Exact 4:3. Integer 2× on 1024×768 panels, the typical 4:3 LCD board resolution. The references hold about 64 columns × 32 rows of text, which fits this canvas at 8 px cell width. |
| Cell | **8 × 12** (`phosphor_8x12`) | 64 cols × 32 rows. 8×16 at 512×384 would give only 24 rows, too few for the header + command + body + soft keys + info frame. |
| Small font | 8 × 8 (`phosphor_8x8`) | Dense labels: milestone tags, soft-key captions, signal readouts. |

This replaces the PLAN.md §4 default (640×480, 8×16). §4 lists 512×384 as the alternative and leaves the
choice to M0. All placement goes through `ui::cells()` / `px()` / `py()`, so only `layout.h` changes if this
is revisited.

## 2. Shared frame (every page except TESTCARD)

| Row | Content | Reference |
|---|---|---|
| 0 | `INDEX` (cols 0–7, clickable), header bar `*** PAGE <id> READY ***` (cols 8–51, inverse), right block: BPM + signature | All references |
| 1 | `COMMAND:` + command field (cols 8–51); right block: position `BAR:BEAT:TICK` | All references |
| 2–28 | Page body (27 rows) | |
| 29–30 | Soft-key row: F1–F12 + `P` (back) + `S` (setup), two rows tall, caption from `kPageCatalog.short_name` | Icon key row in references |
| 31 | `INFO:` message line; right side project name, CPU %, voice count | All references |

Intended deviations from the references:
- Soft keys show text captions (F-number + 5-letter page name), not drawn icons. Icons may come later as our own art.
- The right block shows BPM and signature of the playing (or edited) section on row 0, and the section letter with
  BAR:BEAT:TICK plus a transport glyph (square stopped, triangle playing, blinking dot recording / count-in) on
  row 1. The current instrument is shown in the Page 7, 8 and R headers.
- No product logo copy: Page 1 draws an original monitor + decaying-sine emblem and the PHOSPHOR wordmark.
- Themes: GREEN, AMBER, GREY, VAPOR (PLAN.md §4) **plus CYAN**, matching the cyan reference variants.

## 3. Page set

Numbering follows the references where the function matches; PHOSPHOR-only functions take free letters.

| Id | Name | Milestone | Reference | Notes |
|---|---|---|---|---|
| 1 | INDEX | M0 ✔ | page-1-index | Two columns: emblem left, clickable page list right, `PROJECT :` field below. Unbuilt pages greyed with milestone tag. |
| 2 | DISK CONTROL | M3 ✔ | – | FOLDER line with UP button; list (rows 4–24) of folders (DIR) and audio files with sizes, selection inverse; ENTER opens a folder or imports into the current instrument, A auditions, BACKSPACE goes up; hint and import-status lines (26–27). Projects join in M4. After M8: instrument bundles (`.phinst`, marked INS) are listed after projects; ENTER loads one into the current instrument. `LIBRARY` opens the library folder. `.mid` files: ENTER imports the song with instruments. |
| 3 | KEYBOARD CONTROL | M2 ✔ | – | MIDI INPUTS box (rows 2–9): per input ON/OFF, name, channel filter (ALL, 1–16), state (OPEN / MISSING / OFF / error), backend name. INPUT box (rows 10–16): TIMING (IMMEDIATE / STABLE), VELOCITY curve, BEND RANGE, KEYJAZZ + OCTAVE (shared with Page K), SOUND = PANIC (all sound off, also MOD+ESC), MASTER CC with LEARN / CLEAR, last CC received. LATENCY REPORT box (rows 17–28): block size, events, avg/min/max/jitter, notes, xruns, lost/early/late, histogram 0–6 ms in 0.25 ms bars, RESET. The latency report lives here rather than on Page S (PLAN §7.6) because Page S has no room left; Page S points to it. M8: an IN / OUT switch (row 3, or PAGE UP / DOWN, or `P3 OUT`) shows the OUT view: MIDI OUTPUTS (rows 2–9: ON / OFF, name, CLOCK, state), OUTPUT (MIDI OUT, THRU, OFFSET MS, LATENCY COMP), TRACKS (rows 14–28: 16 tracks in two columns with DEST INT / EXT / BOTH, PORT (FIRST = first open output), CH, PROG), send statistics. |
| 4 | HARMONIC PROFILES | M5 ✔ | – | Fields HARMONIC, SEG, OF (segment count), NORM, COMPUTE. Top: the selected harmonic's amplitude over all segments as bars (pen draws), loop range underlined. Bottom: the 32-harmonic spectrum of the cursor segment (pen draws). |
| 5 | SYNTH | M5 ✔ | page-fx-control (layout idea only) | SYNTH instrument: OSCILLATORS column (osc 1/2 wave and level, osc 2 semitones and cents, pulse width, sub, noise, unison, spread) and LFO AND GLIDE column (shape, rate, sync, depth to pitch / cutoff / PW / amp, glide, mono). DRUM instrument: 16-pad table (note, type, tune, decay, tone, level, pan, choke, sample). Field columns instead of the reference's fader banks; filter and envelopes stay on Page 7. |
| 6 | WAVEFORM DRAWING | M5 ✔ | – | Fields SEG, OF, CYCLES, LOOP, LEN, XFADE, AUTHENTIC, OTHER SEG. Waveform window (rows 5–19) with the previous segment dimmed; pen draws freehand or a LINE between two clicks. Tools FREE, LINE, SMOOTH, COPY TO, INTERP FROM, CLEAR. |
| 7 | CONTROL PARAMETERS | M3 ✔ | page-7-control-* | Header: instrument and sample. Framed block (rows 4–16), three columns of `NAME = [value]`: SOUND (volume, pan, root, fine, interpolation, velocity→amp, instrument), AMP ENVELOPE + LOOP POINTS, FILTER + FILTER ENVELOPE. Lower panel: amp and filter envelope shapes. COPY/PASTE and MODE from the reference are not used yet. |
| 8 | SOUND SAMPLING | M3 ✔ | – | Header: sample, length, rate, channels, peak, memory. Waveform (rows 4–13): min/max and RMS per column, selection band, dotted loop markers with L tags; clicking places the focused marker. Fields: SEL START/END, LOOP mode, LOOP FROM/TO, XFADE MS. Buttons: TRIM, FADE IN, FADE OUT, NORMALIZE, REVERSE, XFADE LOOP / SEL ALL, SEL=LOOP, PLAY, UNDO, REDO. |
| 0 | SPECTRAL DRAWING | M5 ✔ | page-0-spectral-drawing | Top bar: ADD / REMOVE / INVERT, BRUSH 1/2/3, CLEAR, SMOOTH, CREATE (= COMPUTE). Left: the harmonic matrix (segments across, harmonics 1–32 up) painted with the pen. Right: the same data as a 3D surface. The reference's ROUGHNESS / TIME lanes are left out; SMOOTH blurs along time. |
| C | CHIP VOICES | M6 ✔ | – | GB view: CH, DUTY, QUANT, RAW, ENV vol/dir/pace, LEN, SWEEP pace/dir/shift, NOISE width, WAVE LVL, VIB / SPD / DLY, PORTA, STRICT GB; 32 x 16 wave grid and a macro lane (VOL / DUTY / ARP / PITCH / WAVE with LEN, LOOP, REL), both drawn with the pen. SNES view: RATE, ALIGN, A/B, ROOT, ENV ADSR/GAIN (AR DR SL SR, gain mode + value), VOL L/R, NOISE + CLK, PMOD, ECHO, STRICT SNES; envelope curve (1 s held, then released); song echo EDL / EFB / EVOL with 8 FIR faders and presets; ARAM budget bar. |
| D | WAVEFORM DISPLAY | M5 ✔ | page-d-waveform-* | Boxed option bar FORMAT A (oblique) / B (steep) / C (flat overlay, no hidden lines), END SEG, STEP above a full-width waterfall of 128 slices with hidden-line removal, shaded by depth. Wavetables: the segments filled in as they play, real segments bright. Samples (SAMPLER, SNES): second bar SAMPLE CYCLE / SPAN and RANGE ALL / LOOP. |
| K | VIRTUAL KEYBOARD | M2 ✔ | page-k-virtual-keyboard | KEYJAZZ page: boxed `OFF / ON` toggle, piano strip C2–B6 lighting every held note (KEYJAZZ and MIDI input) with the KEYJAZZ range underlined, key legend for both rows (Z-row base octave, Q-row one up; caps light while held), OCTAVE value with `[ DOWN` / `] UP` buttons. While KEYJAZZ is on the command line shows a hint and takes no typing; ESC turns it off, MOD+K toggles it anywhere. |
| L | LOOP LIBRARY | M7 ✔ | – | Rows 3–10: eight slots — number, play state (`>` playing, blinking when waiting for its launch), NAME, BARS, loop BPM (from length and bars), SYNC (VARI / STRETCH / SLICE), QUANT (NOW / BEAT / BAR), TRK (output track), position bar with bar ticks (click or ENTER plays / stops). Frame rows 12–25: the selected slot — GAIN, LP, REV, SCREW, SHIFT, SLICES, BEATS/BAR, FROM INST + CLEAR, source info, waveform with slice and bar marks and the playhead. Row 26: VAPOR bar 0–100. Row 27: help. No reference image; designed in the screen vocabulary of the other pages. |
| M | MIXER | M4 ✔ | page-o-output-mix | Bank selector TRACKS 1-8 / A-H (TAB). Eight strips + MASTER: instrument name, post-fader peak meter (-60..0 dBFS) beside a fader with 6 dB ticks (-60..+6 dB), value, PAN, M / S toggles, track label. M7: rows 21–24 per strip insert 1, insert 2, send A, send B; the master strip has master inserts 1–2 and returns A / B instead. +/- on an insert cycles the effect type; ENTER (or a click) opens it in the panel on rows 25–27 (up to five parameters; a return's last one is its LEVEL). Strip names show LOOPS lanes and tracks that loop slots play on (`L1 NAME`). |
| R | REAL-TIME COMPOSER | M4 ✔ | page-r-composer-*, page-r-song | Header (rows 2–4): SEC, PAT (+ name), STEPS, GRID / BPM, SIG, SWING, MODE, METRO / CNT (count-in), QUANT, REPEAT, current track and instrument. Grid (rows 5–21): ruler with beat numbers, 16 lanes `1–8 A–H` with instrument and M/S/W flags, up to 32 steps visible (scrolls), note bars with length, beat/bar lines, playhead, cursor. Row 22 cursor note info, row 23 PLAY STOP CONT REC + state and song step, PANIC at the right end (position is in the status header), row 25 song ORDER (section letters, playing step highlighted). One page instead of the reference's separate song view; the order row covers it. M8: `PR AUTO [cc]` swaps the note grid for the AUTO view: one CC lane of the current track as a bar per step (rows 7–20), the strip on row 21 clears steps when drawn on, lane list on the left (`*` = has values), cursor value on row 22; `PR NOTES` returns. FOLLOW (row 2, right; `FOLLOW ON|OFF`, saved in settings.json, on by default): while playing, the grid pages along with the playhead and, in SONG mode, the page shows the playing section. |
| S | SETUP | M0 ✔ display, M1 ✔ audio | – | DISPLAY box (rows 2–18; SIDE KEYS OFF / AUTO / ON on row 15). AUDIO / MIDI box (rows 19–28): DEVICE + NEXT, status line (rate, period × periods, ms, channels), OUTPUT and EXCLUSIVE toggles, RATE, BUFFER, TEST (OFF / TONE / CLICK), MASTER DB, engine stats line (CPU, peak, xruns, overloads, guard). MIDI rows join in M2/M8. |
| H | HELP | M0 ✔ (stub) | – | Help for the previous page, command list, key list. |
| TESTCARD | Test card | M0 ✔ | – | Full canvas, no frame: 32 px grid, 1 % overscan ticks per edge, large circle, centre cross. Any key returns. |

## 4. Interaction (all pages)

- Command line always active. `P<id>`, F1–F12 and clicks on the soft keys or INDEX lines switch pages.
- Arrow keys move the field focus; `+`/`-` or left/right change values (Shift ×10); Enter edits; Esc returns to the
  command line.
- Light pen (mouse): OS cursor hidden, crosshair drawn on the canvas, mapped through the inverse letterbox.
  Inverse-video zones are clickable or drawable.

## 5. Decisions (approved 2026-11-25)

1. Canvas 512×384 with 8×12 cells: kept.
2. Virtual keyboard: on both Page 3 (section) and its own Page K (`K` was unused).
3. Spectral drawing: kept as Page 0 (M5, with the wavetable engine).
4. CYAN: kept as the fifth theme.
5. COLOR: a sixth theme (owner request, 2026-11-26). White text and lines on near-black, with role
   colours: title tabs and banner cyan, selection / focused field amber, text cursor, playheads and
   hover magenta, mixer meters green (to -18 dBFS) / yellow (to -6) / red, running things (transport,
   playing loops, held keys) green, field labels pale blue, the depth ramp (3D waterfall, spectral
   matrix) violet -> magenta -> cyan. The role inks (`gfx/palette.h`, 16..23) take the colour of the
   ink they replace in the single-hue themes, so those look exactly as before.
6. PAGE KEY (owner request, 2026-11-26): modifier + page id jumps to that page (P = back). Page S
   row 11 or `PAGEKEY OFF|CTRL|ALT|CMD`; stored in settings.json. Default CTRL on macOS (MOD is Cmd),
   ALT elsewhere. It takes precedence over key-map chords with the same keys; Page S lists those.

## Side keys (beside the canvas)

Not part of the 512 × 384 canvas: on screens wider than 4:3 the host widens the screen texture by a strip of
48–112 canvas pixels on each side (`gfx::compute_screen_layout`) and the UI draws five keys per strip
(`ui/side_keys.h`). Left: INDEX, PAGE ◄, PAGE ►, BACK, ESC. Right: ▲, ▼, +, −, ENTER. + / − use the
page's own +/- and fall back to RIGHT / LEFT; ▲ / ▼ / + / − repeat while held (0.4 s, then 12/s). The CRT
pass covers the strips too. Headless renders and golden images stay 512 × 384 without strips.

