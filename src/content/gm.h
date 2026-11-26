#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "io/smf.h"
#include "model/workspace.h"

// MIDI import with instruments ("fast project assembly"): every imported track gets an instrument
// in its slot (track n -> instrument n) if that slot is still empty. The instrument library is
// asked first: an instrument named like the MIDI track ("BASS"), then one named like the General
// MIDI family of its program ("SYNTH LEAD", "STRINGS"; drums on channel 10: "DRUMS"). Otherwise a
// built-in preset for the family is used.
namespace phos::content {

// General MIDI level 1 program families (16 x 8 programs), e.g. 33 -> "BASS"; -1 -> "".
const char* gm_family(int program);
// The built-in preset for a program (-1: a general synth), or a drum kit.
model::Instrument gm_instrument(int program, bool drums);

struct MidiAssembly {
    int from_library = 0;
    int built_in = 0;
    int kept = 0; // slots that already had an instrument
    std::vector<std::string> lines; // "1 BASS <- LIBRARY BASS", for the console / INFO line
};

// Puts `imported.song` into `doc` and fills the instrument slots of the imported tracks.
MidiAssembly assemble_midi(model::Document& doc, const io::SmfImport& imported, const std::filesystem::path& library,
                           std::uint32_t sample_rate);

} // namespace phos::content
