# Porting PHOSPHOR to iPadOS / iOS and Android: analysis and plan

Status: proposal (2026-11-26). Nothing here is decided; the open decisions are marked ★. If they are
accepted, the architecture changes get an ADR (`docs/adr/`) before work starts, as CLAUDE.md asks.

## 1. Summary

Most of PHOSPHOR already runs on a phone or tablet CPU unchanged: the engine, the model, the
sequencer, all DSP and the whole UI (it draws into a CPU canvas). The desktop-only part is small and
already behind a `platform/` layer: the GLFW window and input, the OpenGL 3.3 CRT renderer, settings
paths and assets read from next to the executable. The real work is not the code port but **touch**:
the UI is built for a keyboard and a mouse-sized light pen, and its cells are small.

Recommendation:

1. Do it after M9 (the Pi 5 prototype device) and M10 (Windows) as two milestones: **iPadOS first**, then **Android tablets**. Phones only
   as a later, reduced mode (§6.3).
2. Replace GLFW + OpenGL with **SDL3 + SDL_GPU** on every platform (★ D1): one window, input,
   lifecycle and renderer layer for macOS, Windows, Linux, iOS and Android; Metal on Apple, Vulkan /
   D3D12 elsewhere. This also retires PLAN.md §12's "OpenGL is deprecated on macOS" risk.
3. Make the UI touch-capable on the desktop first (drag-to-change fields, multi-touch, a touch strip),
   so most of the UX work is tested with the existing golden-image and UI tests before any device build.

Rough size: groundwork 3–4 weeks, iPad 4–6 weeks, Android 4–6 weeks, phones 2–3 weeks (one developer,
including device testing). The estimates are coarse; touch UX iteration is the uncertain part.

## 2. What ports as it is

| Layer | Contents | Mobile |
|---|---|---|
| `phosphor_core` | engine, voices, chips, DSP, FX, loops, sequencer, model, projects, SMF, WAV, offline render | Portable C++20. ARM64 flush-to-zero is already in `core/denormals.h` (macOS arm64 is the main target). `std::filesystem` and threads work on iOS and on Android (NDK r22+). |
| `phosphor_ui` | pages, widgets, chrome, commands, key map, canvas, palette | Portable (no GL, no OS calls). Needs input and layout additions (§5). |
| Dependencies | nlohmann/json, stb, r8brain, Signalsmith Stretch / Linear, miniaudio, libremidi | All permissive (MIT / BSD / PD / zlib), so app-store distribution is fine. miniaudio: CoreAudio (iOS), AAudio and OpenSL ES (Android). libremidi 5.4.3: CoreMIDI (iOS) and an Android AMidi backend. Signalsmith Linear uses Accelerate on Apple (iOS too). |
| Tests | Catch2 unit tests, golden images, offline renders | Keep running on the desktop host; they cover the shared code. |

## 3. What does not port

| Part | Today | Problem on mobile |
|---|---|---|
| Window, input, monitors | `platform/window_glfw.*` (348 lines) | GLFW has no iOS or Android support. |
| CRT renderer | `platform/crt_renderer_gl.*` (285 lines), GLSL 330 shaders, glad | OpenGL ES is deprecated on iOS (works, but no future); Android needs GLES 3.0 or Vulkan; shaders need `#version 300 es` / MSL / SPIR-V. |
| Assets | fonts, shaders, conf read from `exe_dir()` | iOS: app bundle resources; Android: inside the APK (AAssetManager), not on the file system. |
| Settings and projects | `platform/paths.cpp`, `~/Documents/PHOSPHOR` | Sandboxed: app container folders; user files come in through the system document picker (security-scoped URLs on iOS, Storage Access Framework `content://` on Android). |
| Page 2 file browser | browses any folder | Only the app's own folders are browsable; everything else via the system picker. |
| Project and instrument bundles | folders (`NAME.phosphor/`, `NAME.phinst/`) | iOS can declare them as document packages (one item in Files); Android SAF handles folders poorly. |
| MIDI device access | libremidi enumerates ports | iOS: USB / network work directly, **Bluetooth MIDI needs Apple's pairing view** (`CABTMIDICentralViewController`). Android: AMidi needs a `MidiDevice` opened in Java (`MidiManager`, JNI), API 29+. |
| App shell | `App` uses `platform::Window` and `CrtRenderer` directly; CLI tools | Needs an app-lifecycle model (suspend, resume, memory warnings, audio interruptions); the CLI tools stay desktop-only. |

## 4. Platform layer: options (★ D1)

| Option | Window / input / lifecycle | Renderer | Pros | Cons |
|---|---|---|---|---|
| **A. SDL3 + SDL_GPU everywhere** (recommended) | SDL3 (zlib, release 3.4.x) | SDL_GPU: Metal, Vulkan, D3D12; shaders cross-compiled offline (SDL_shadercross) | One code path for five platforms; touch, pen (pressure, hover), IME / on-screen keyboard, text input, lifecycle events, file dialogs, Android Java glue (SDLActivity) included; replaces GLFW + glad + OpenGL on the desktop too | A rewrite of window + renderer (~650 lines) and desktop re-verification; a new dependency set |
| B. SDL3 on mobile only, GLFW + GL stays on the desktop | SDL3 on mobile | GLES 3.0 (Android), Metal (iOS) | Desktop untouched | Two window layers and three renderers to maintain |
| C. sokol_app + sokol_gfx | sokol (zlib, header-only) | Metal, GLES3, D3D11 | Very small | Weaker text input / IME, no file dialogs, more own Android / iOS glue |
| D. Native shells (UIKit + MTKView, Android GameActivity) | own | own Metal / GLES | Full control | Most work, most code per platform |

The renderer is small (an R8UI canvas texture, a 24-entry palette, phosphor persistence ping-pong,
bloom blur, CRT pass), so any option is feasible; A gives the most per line written. The CRT pass is
not covered by golden images today (they test the canvas); a port should add a small headless
offscreen render test of the post pass (compare to a reference with a tolerance).

## 5. Touch and small screens: the real work

### 5.1 Sizes

The canvas stays 512 × 384 (4:3); a cell is 8 × 12 canvas pixels. Fitted to the screen height:

| Device (landscape) | Points / dp | Scale | One cell | Touch target guideline |
|---|---|---|---|---|
| iPad 11" | 1194 × 834 pt | 2.17 | 17 × 26 pt | 44 × 44 pt |
| iPad 13" | 1366 × 1024 pt | 2.67 | 21 × 32 pt | 44 × 44 pt |
| Android tablet 11" | ~1280 × 800 dp | 2.08 | 17 × 25 dp | 48 × 48 dp |
| iPhone 6.1" | 852 × 393 pt | 1.02 | 8 × 12 pt | 44 × 44 pt |

Fields are 2–12 cells wide but only one row (25–32 pt) high: usable with a stylus, tight with a finger
on a tablet, unusable on a phone.

### 5.2 Interaction model (all testable on the desktop with a mouse)

- **Tap = focus, drag = change.** A tap focuses a field (as a click does now); a vertical drag on a
  focused field changes it (+/- per few points, faster with distance), a double tap is ENTER (toggle,
  activate). This replaces wheel and +/- keys, which touch does not have.
- **Touch strip** in the margins the 4:3 canvas leaves (tablet screens are ~1.43:1): large buttons for
  − / + (and big steps), ENTER, ESC, the command keyboard, PLAY / STOP, PANIC, and the page soft keys
  in a bigger size. The canvas keeps its 4:3 look; the strip is drawn outside it (on the CRT "bezel").
- **Pen = light pen.** Apple Pencil / S Pen draw on Pages 0, 4, 6, 8 and the automation lanes with
  precision; Pencil hover (iPad Pro) gives the zone highlight a mouse gives today.
- **Multi-touch** for Page K (play chords on the virtual keyboard) and the mixer (two faders): the UI's
  single pen (`InputState`) becomes a list of touches; one-pen pages keep using the first.
- **Text**: the command line opens the system keyboard (SDL text input). Hardware keyboards work as
  on the desktop; iPad keyboards have no F-keys, so the PAGE KEY (CMD+K etc.) is the page shortcut.
- **Zoom** (phones, §6.3): a two-finger pinch magnifies a quarter of the canvas.

## 6. Per platform

### 6.1 iPadOS (first: closest to the desktop, best screen ratio, Pencil, USB-C MIDI)

- Build: CMake with the Xcode generator and `CMAKE_SYSTEM_NAME iOS` (arm64, simulator arm64); assets as
  bundle resources; Info.plist; signing with an Apple Developer account (★ D5).
- Audio: miniaudio CoreAudio + AVAudioSession: category playback (playAndRecord once input sampling
  exists), preferred 48 kHz and a 128-frame IO buffer; handle interruptions (calls) and route changes
  (headphones, USB interfaces) by reopening the device; `UIBackgroundModes: audio` so a song and MIDI
  keep running in the background (★ D4).
- MIDI: libremidi CoreMIDI as now; a "BLUETOOTH MIDI" button on Page 3 presents Apple's pairing view.
- Files: Documents folder visible in the Files app (`UIFileSharingEnabled`,
  `LSSupportsOpeningDocumentsInPlace`); `.phosphor` and `.phinst` declared as document packages; IMPORT
  and LOAD of outside files through the document picker (copied into the app).
- Lifecycle: autosave when the app goes to the background; drop STRETCH caches and audition data on
  memory warnings.
- Store: privacy manifest (required-reason APIs: file timestamps, UserDefaults), usage strings for
  Bluetooth (and microphone later).
- AC (proposal): latency key-to-sound ≤ 10 ms at 128 frames on an iPad with A14 or newer; no xruns in 10
  minutes of GB ADVENTURE + VAPOR PLAZA; every page fully operable by touch alone; a project round trip
  through the Files app; BLE and USB MIDI keyboards play.

### 6.2 Android tablets

- Build: Gradle project with the NDK (CMake `externalNativeBuild`), arm64-v8a (+ x86_64 for the
  emulator), minSdk 29 (AMidi), target the current SDK the Play Store requires; native libraries linked
  for 16 KB pages (required by Google Play for Android 15+ devices).
- Audio: miniaudio AAudio, low-latency performance mode, exclusive sharing where the device allows;
  OpenSL ES fallback. Latency varies widely by device; report it on Page S as today.
- MIDI: Java / Kotlin glue for `MidiManager` (USB and BLE device open, permissions), then libremidi's
  AMidi backend; about 200 lines of JNI.
- Files: app-specific storage for projects and the library; SAF for import / export. Because SAF does
  not handle folders well, add a **single-file archive** for sharing (★ D6): `NAME.phosphor.zip` /
  `NAME.phinst.zip` (miniz, MIT), also useful on the desktop.
- Assets read from the APK (SDL_IOFromFile handles it; an asset-reading interface replaces `exe_dir()`).
- AC (proposal): runs on a current Pixel tablet and a Samsung Galaxy Tab S9 class device; key-to-sound
  latency measured and documented (target ≤ 20 ms on "pro audio" devices); no xruns in 10 minutes; touch
  AC as on iPad; USB and BLE MIDI.

### 6.3 Phones (iOS and Android, later; ★ D3)

At 1:1 scale the full UI is not finger-operable. Options: (a) the full UI with pinch-zoom and the touch
strip (works, clumsy); (b) a reduced "pocket" mode: Pages R, K, L, M, C with a phone layout (a second
canvas size, 384 × 216 cells?), the rest read-only; (c) no phone build. Recommendation: (a) as a
by-product of the tablet work, (b) only if there is demand.

## 7. Plan

| Step | Where | Content | Exit |
|---|---|---|---|
| G1 | desktop | Platform interfaces: `Window` (events, touches, text input, lifecycle), `Renderer`, `Assets` (read files), `Paths`, `FilePicker`; `App` uses only these; `paths.cpp` per platform | No behaviour change; all tests and the golden images unchanged |
| G2 | desktop | Touch model in the UI: touch list in `InputState`, drag-to-change, double tap, touch strip (behind a setting on the desktop), multi-touch Page K; UI tests for each | Every page operable with mouse-only "touch" gestures; tests |
| G3 | desktop | (if D1 = A) SDL3 + SDL_GPU window and renderer for macOS / Windows / Linux, shaders cross-compiled; post-pass render test | Desktop at parity with GLFW + GL (shots by `tools/screenshots.sh` compared side by side); GLFW and glad removed |
| I1 | iPad | Xcode / CMake iOS target, bundle, AVAudioSession, CoreMIDI + BLE pairing, Files integration, lifecycle, privacy manifest | iPad AC (§6.1) |
| A1 | Android | Gradle + NDK, SDL3 Android, AAudio, MIDI JNI glue, SAF + zip archives, 16 KB pages | Android AC (§6.2) |
| P1 | phones | Pinch-zoom, phone strip layout (option a) | Usable on a 6" phone in landscape |
| Later | iOS | AUv3 plugin with `phosphor_core` (play PHOSPHOR instruments inside other iOS apps); Android: a MIDI device service | — |

Each step becomes a milestone section in PLAN.md (M11 …) once the decisions are made; the CLI tools
(`--render-audio`, `--analyze`, `--midi-loopback`) stay desktop-only.

## 8. Risks

- **Touch UX of a dense, keyboard-first UI.** The biggest unknown; mitigated by building it on the
  desktop first (G2) and trying it on a real tablet early (a G2 build can run on the iPad as soon as I1's
  shell exists).
- **Android audio latency and device spread.** AAudio low-latency paths differ by vendor; some devices
  will stay above 20 ms. Document measured devices.
- **Folder bundles on mobile file systems.** Solved by document packages (iOS) and zip archives (both).
- **CRT shader port.** Visual regressions in the post pass are not caught by today's golden images;
  add an offscreen render test (G3).
- **Store reviews and accounts.** Apple Developer Program and Google Play developer accounts, privacy
  manifests and data-safety forms; TestFlight / internal testing first.
- **Maintenance.** Five platforms for one developer; option A (one platform layer) keeps it manageable.

## 9. Decisions (★)

| # | Question | Recommendation |
|---|---|---|
| D1 | Platform layer: SDL3 + SDL_GPU everywhere, mobile-only, sokol, or native? | SDL3 + SDL_GPU everywhere |
| D2 | Order: iPad first, then Android? | Yes |
| D3 | Phones in scope (full UI with zoom / pocket mode / none)? | Full UI with zoom, later |
| D4 | Keep playing in the background on iOS? | Yes (external MIDI gear keeps running) |
| D5 | Distribution: App Store / TestFlight, Play Store / sideload; who holds the developer accounts? | TestFlight and Play internal testing first |
| D6 | Single-file `.zip` archives for projects and instruments? | Yes (needed for Android, useful everywhere) |
| D7 | AUv3 plugin later? | Keep the engine separable so it stays possible; decide after I1 |
