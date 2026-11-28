# ADR 0001: Prototype device (Raspberry Pi 5) before Windows

Date: 2026-11-27. Status: accepted (owner decision).

## Context

PLAN.md had Windows hardening and packaging as M9 and Linux / Raspberry Pi 5 as M10 ("later"). The owner
now wants a standalone instrument: a 3D-printed case with a Raspberry Pi 5, a 5–7" touch display,
hardware buttons, knobs and a value wheel. That device is now the main target after macOS.

## Decision

- **M9** is the prototype device: Linux arm64 on a Raspberry Pi 5, the 7" Touch Display 2, a USB-MIDI
  controls board, a USB audio interface, start-up straight into PHOSPHOR, and the hardware itself
  (concept in `docs/hardware/concept.md`).
- **M10** is Windows hardening and desktop packaging (the former M9, unchanged).
- The side keys (screens wider than 4:3) are the touch layer for the device's 16:9 panel. They are
  already built, so M9 starts with them.
- CLAUDE.md and PLAN.md list the targets as macOS → Pi 5 device → Windows.

## Consequences

- **Linux support moves forward.** ALSA audio and MIDI, an OpenGL ES 3.1 renderer path (the Pi's GPU
  driver offers GL 3.1 / GLES 3.1, not the GL 3.3 core profile used on macOS) and a kiosk start-up become
  M9 work.
- **The window-layer choice is needed now, not after Windows.** That is D1 of `docs/porting-mobile.md`:
  SDL3 can draw without a desktop (KMS/DRM); GLFW needs a Wayland compositor such as `cage`.
- **New repo content:** controls-board firmware (C++, Pico SDK) and the enclosure files (OpenSCAD) go
  into `hardware/`, with the same rules as the app: permissive licenses, pinned dependencies.
- **The ★ license decision (§12)** stays open until M10 (Windows, ASIO).
- **No code is removed.** Windows keeps building where it builds today; its hardening only moves later.
