# Fonts

Original PHOSPHOR bitmap fonts (no third-party glyphs).

* `src/phosphor_8x12.txt`: main 8×12 cell font, full ASCII plus symbols at 0x80–0x8F (arrows, triangles,
  LED dot/ring, play/stop, note, block). Edit the `#`/`.` art.
* `src/phosphor_8x8.txt`: small caps-only font for soft keys and annotations (same symbol code points).

After editing, regenerate the runtime atlases (PNG + metrics JSON):

    cmake --build --preset <preset> --target fonts

`phosphor_tests` fails if a source and its packed atlas differ.
