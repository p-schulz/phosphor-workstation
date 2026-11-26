#pragma once

#include <memory>

#include "ui/page.h"

namespace phos::ui {

std::unique_ptr<Page> make_index_page();    // Page 1
std::unique_ptr<Page> make_setup_page();    // Page S (display section in M0)
std::unique_ptr<Page> make_help_page();     // Page H (stub)
std::unique_ptr<Page> make_disk_page();     // Page 2 (folder browser, import, audition)
std::unique_ptr<Page> make_keyboard_page(); // Page 3 (MIDI inputs, timing, latency report)
std::unique_ptr<Page> make_control_page();  // Page 7 (SAMPLER parameters)
std::unique_ptr<Page> make_sample_page();   // Page 8 (sample editor)
std::unique_ptr<Page> make_composer_page(); // Page R (pattern grid, transport, song order)
std::unique_ptr<Page> make_mixer_page();    // Page M (track mixer)
std::unique_ptr<Page> make_loops_page();    // Page L (loop slots, VAPOR)
std::unique_ptr<Page> make_harmonics_page(); // Page 4 (harmonic profiles)
std::unique_ptr<Page> make_synth_page();     // Page 5 (SYNTH / DRUM)
std::unique_ptr<Page> make_wavedraw_page();  // Page 6 (waveform drawing)
std::unique_ptr<Page> make_waveform_page();  // Page D (3D waterfall)
std::unique_ptr<Page> make_spectral_page();  // Page 0 (spectral drawing)
std::unique_ptr<Page> make_chip_page();      // Page C (GB / SNES)
std::unique_ptr<Page> make_vkeys_page();    // Page K (KEYJAZZ virtual keyboard)
std::unique_ptr<Page> make_testcard_page(); // TESTCARD overlay

} // namespace phos::ui
