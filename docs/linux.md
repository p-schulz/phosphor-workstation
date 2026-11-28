# PHOSPHOR on Linux: Raspberry Pi 5, Pi 4 and the uConsole

First step of M9. The Linux build is new: it compiles cleanly with GCC 16 and libstdc++, and all tests pass
there, but it has not run on a Pi yet. Please send back what the steps below print.

## 1. The SD card

- Use a **64-bit** Raspberry Pi OS with desktop (Bookworm or newer).
- On the uConsole, use an image made for it: the stock image lacks the uConsole's display and keyboard
  drivers. A second card keeps your retro setup untouched.
- The board needs internet access: the build fetches its pinned dependencies from GitHub.

## 2. Get the code onto the device

Over the network from the Mac, without the build folders (replace user and host):

```sh
rsync -a --exclude build ~/phosphor-workstation/ pi@uconsole.local:phosphor-workstation/
```

Or `git clone` it there, if the repository is pushed.

## 3. Build and test

```sh
cd ~/phosphor-workstation
tools/setup-linux.sh
```

The script does four things:
- installs the packages (compiler, CMake, Ninja, ALSA, X11 / Wayland, GL headers);
- allows real-time priority for the `audio` group and adds you to it;
- builds the release version;
- runs the tests.

On a Pi 4 or CM4 the first build takes roughly 20–40 minutes; later builds only recompile what changed.
**Log out and in once** afterwards, so the `audio` group membership takes effect.

## 4. Start

```sh
tools/run-linux.sh                                  # window
tools/run-linux.sh --exec "DISPLAY FULL"            # fullscreen (side keys appear on 16:9)
```

On Pi hardware the script sets `MESA_GL_VERSION_OVERRIDE=3.3`. The Pi's GPU driver officially offers
OpenGL 3.1, and PHOSPHOR asks for 3.3 until the OpenGL ES renderer exists.

**On a Pi 4 / CM4** (uConsole), save the GPU for the essentials: `THEME LCD` or `CRT OFF` skip the bloom
passes (two of the four GPU passes per frame). The build is already tuned for the board's CPU
(`PHOS_NATIVE`).

**First things to try:**
- **Page S:** pick the audio device (the USB interface if you have one).
- **Buffer:** start at 256 or 512 frames (`AUDIO BUFFER 512`), then try lower until it crackles.
- **Songs:** `LOAD examples/projects/GB ADVENTURE.phosphor`, then Space. The other example projects
  are good tests too.
- **Navigation:** the trackball is the light pen; the side keys or the keyboard move between pages.

## 5. What to send back

| What | How |
|---|---|
| Build or test errors | the end of the `setup-linux.sh` output |
| Graphics driver | `glxinfo -B \| head -20` |
| Audio devices | `build/linux-release/phosphor --list-devices` |
| MIDI timing | `build/linux-release/phosphor --latency-report --seconds 10` (with a MIDI keyboard connected) |
| Load | Page S, the engine line (CPU, xruns) while an example plays |
| DSP speed (no graphics) | `build/linux-release/phosphor --render-audio "examples/projects/GATED DRUMS.phosphor" --out /tmp/t.wav` (prints "×× real time"; the M2 Pro does 48×) |
| Temperature | `vcgencmd measure_temp` after a few minutes of playing; `vcgencmd get_throttled` (0x0 = never throttled) |
| Screenshot | `SHOT` on the command line (saved in `~/.config/phosphor/snaps/`) |

## Troubleshooting

| Problem | Try |
|---|---|
| "failed to create window" | Start with `tools/run-linux.sh`, not the binary directly (GL override). Still failing: `PHOS_GLFW_PLATFORM=x11 tools/run-linux.sh` |
| Black or garbled picture | `CRT OFF` on the command line (rules the post pass in or out) |
| Crackles | larger buffer (`AUDIO BUFFER 512`); check Page S for xruns |
| No MIDI device | `aconnect -l` lists ALSA MIDI ports; `MIDI RESCAN` in PHOSPHOR |
| The build runs out of memory | it already uses 2 jobs below 3 GB RAM; add swap or run `cmake --build --preset linux-release -j 1` |
