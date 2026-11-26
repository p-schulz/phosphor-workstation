#include "content/gm.h"

#include <array>
#include <cctype>

#include "content/presets.h"
#include "io/instrument_io.h"

namespace phos::content {

namespace {

constexpr std::array<const char*, 16> kFamilies = {
    "PIANO", "CHROMATIC PERC", "ORGAN", "GUITAR", "BASS", "STRINGS", "ENSEMBLE", "BRASS",
    "REED", "PIPE", "SYNTH LEAD", "SYNTH PAD", "SYNTH FX", "ETHNIC", "PERCUSSIVE", "SOUND FX",
};

std::string upper(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

bool is_free(const model::Instrument& i) { return i.empty() && i.name == "EMPTY"; }

} // namespace

const char* gm_family(int program) {
    return program < 0 || program > 127 ? "" : kFamilies[static_cast<std::size_t>(program / 8)];
}

model::Instrument gm_instrument(int program, bool drums) {
    if (drums) {
        return presets::drum_kit("DRUMS");
    }
    const char* fam = program < 0 ? "SYNTH" : gm_family(program);
    model::Instrument i;
    switch (program < 0 ? -1 : program / 8) {
    case 0:
        i = presets::electric_piano();
        break;
    case 1:
    case 14:
        i = presets::bell(fam);
        break;
    case 2:
        i = presets::organ(fam);
        break;
    case 3:
    case 13:
        i = presets::pluck(fam);
        break;
    case 4:
        i = presets::synth_bass(fam, 700.0f, 2.0f);
        break;
    case 5:
    case 6:
    case 11:
        i = presets::pad(fam);
        break;
    case 7:
        i = presets::brass(fam);
        break;
    case 8:
    case 9:
        i = presets::reed(fam);
        break;
    case 10:
        i = presets::lead(fam);
        break;
    case 12:
    case 15:
        i = presets::sfx(fam);
        break;
    default:
        i = presets::lead("SYNTH");
        break;
    }
    i.name = fam;
    return i;
}

MidiAssembly assemble_midi(model::Document& doc, const io::SmfImport& imported, const std::filesystem::path& library,
                           std::uint32_t sample_rate) {
    MidiAssembly out;
    doc.song = imported.song;
    for (std::size_t t = 0; t < imported.track_info.size() && t < static_cast<std::size_t>(model::kInstruments); ++t) {
        const io::SmfTrackInfo& info = imported.track_info[t];
        doc.song.tracks[t].instrument = static_cast<int>(t);
        model::Instrument& slot = doc.instruments[t];
        if (!is_free(slot)) {
            ++out.kept;
            out.lines.push_back(std::to_string(t + 1) + " " + upper(info.name) + " <- KEPT " + slot.name);
            continue;
        }
        // Library: the track's own name, then the GM family (or DRUMS).
        std::vector<std::string> names;
        if (!info.name.empty()) {
            names.push_back(upper(info.name));
        }
        names.push_back(info.drums ? "DRUMS" : gm_family(info.program));
        bool found = false;
        for (const auto& n : names) {
            const auto path = io::instrument_path(library, n);
            if (n.empty() || !io::is_instrument_bundle(path)) {
                continue;
            }
            std::string error;
            if (auto inst = io::load_instrument(path, sample_rate, &error)) {
                slot = std::move(*inst);
                ++out.from_library;
                out.lines.push_back(std::to_string(t + 1) + " " + upper(info.name) + " <- LIBRARY " + n);
                found = true;
                break;
            }
        }
        if (!found) {
            slot = gm_instrument(info.program, info.drums);
            ++out.built_in;
            out.lines.push_back(std::to_string(t + 1) + " " + upper(info.name) + " <- PRESET " + slot.name);
        }
    }
    return out;
}

} // namespace phos::content
