# Dependencies

All third-party code is pinned in `cmake/Dependencies.cmake` (FetchContent) or vendored under `thirdparty/`.
Keep this file, `cmake/Dependencies.cmake` and `THIRD_PARTY_LICENSES.md` in sync. Licence check before adding
anything: no GPL/LGPL without explicit approval (CLAUDE.md).

| Library | Pin | Source | Licence | Used by | Since |
|---|---|---|---|---|---|
| nlohmann/json | v3.12.0 (release tarball, SHA256 `42f6e95c…ff7aa`) | FetchContent URL | MIT | `phosphor_core` | M0 |
| stb (stb_image, stb_image_write) | commit `6b5799bae8a1fe3792691ef4b72ae26434c3e799` (no tags upstream) | FetchContent git | MIT / Unlicense | `phosphor_ui`, tools | M0 |
| GLFW | 3.5.1 | FetchContent git (shallow) | zlib | `phosphor_platform` | M0 |
| glad | generated with glad2 2.0.8, `gl:core=3.3`, no extensions | vendored `thirdparty/glad/` | (WTFPL OR CC0-1.0) AND Apache-2.0 | `phosphor_platform` | M0 |
| Catch2 | v3.16.1 | FetchContent git (shallow) | BSL-1.0 | `phosphor_tests` only | M0 |
| libremidi | v5.4.3 | FetchContent git (shallow), static library | BSD-2-Clause (parts from RtMidi, MIT, and ModernMidi, BSD-2-Clause) | `phosphor_platform` (MIDI in), `midi_probe` | M2 |
| miniaudio | 0.11.26 | FetchContent git (shallow), header only; implementation in `thirdparty/miniaudio_impl.c` | Public Domain (Unlicense) or MIT-0 | `phosphor_platform` (device I/O, M1), `phosphor_core` (decoders, M3) | M1 |
| stb_vorbis | v1.22, bundled in miniaudio 0.11.26 `extras/stb_vorbis.c` | compiled into `thirdparty/miniaudio_impl.c` | MIT or Public Domain | Ogg Vorbis import | M3 |
| Signalsmith Stretch | 1.4.0 | FetchContent git (shallow, no submodules), header only | MIT | `phosphor_core` (`src/dsp/stretch.cpp`: loop STRETCH cache, offline) | M7 |
| Signalsmith Linear | 0.6.4 (pulled in by Stretch; declared first so only this pin is used) | FetchContent git (shallow), header only; uses Apple's Accelerate framework on macOS | MIT | Signalsmith Stretch (STFT / FFT) | M7 |
| r8brain-free-src | version-6.5 (built-in Ooura FFT, no PFFFT) | FetchContent git (shallow), `r8bbase.cpp` compiled here | MIT (Ooura FFT: free use with notice) | import resampling (`CDSPResampler24`) | M3 |

Build-time only (not linked): CMake ≥ 3.24, Ninja, clang-format (optional `format` target).

miniaudio is built with `MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE`:
device I/O plus the WAV/AIFF (dr_wav), FLAC (dr_flac), MP3 (dr_mp3) decoders, and Ogg Vorbis through the bundled
stb_vorbis. AIFF support in dr_wav at this version is verified by the `tone_A3.aiff` fixture (PLAN.md §3).

In-house instead of a library (M1): SPSC ring (`src/core/spsc_ring.h`, instead of moodycamel), WAV read/write
(`src/io/wav.cpp`, keeps `phosphor_core` free of device code and testable), radix-2 FFT for `--analyze`
(`src/dsp/analysis.cpp`; KissFFT can replace it when resynthesis needs real-input FFTs).

libremidi is built without Boost, JACK, PipeWire, network and computer-keyboard backends; on Windows only
WinMM (Windows MIDI Services / UWP need NuGet downloads at configure time; decide in M10). Its fixed-size
message mode needs Boost, so inputs use the raw-bytes callback instead and PHOSPHOR parses MIDI itself
(`src/core/midi.h`): the callback still never allocates.

`src/dsp/stretch.cpp` is the only file that includes Signalsmith headers; it is compiled with `-O2` in every
configuration so that STRETCH caches render quickly in Debug builds too.

Planned, not yet added (PLAN.md §3): KissFFT, Dear ImGui,
nativefiledialog-extended, Tracy.

## Updating a pin

1. Change the tag/URL/hash in `cmake/Dependencies.cmake`.
2. Delete `build/<preset>/_deps/<name>-*` (or the whole build dir) and reconfigure.
3. Build with `ci-mac` (warnings as errors) and run `ctest`.
4. Update this table and `THIRD_PARTY_LICENSES.md`.
