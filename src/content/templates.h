#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/workspace.h"

// `NEW <template>` (PLAN.md §7.5): ready-to-play vaporwave projects. No third-party audio: every
// template composes a short source pattern with its own instruments and RESAMPLEs it into loop
// slot 1 through the offline engine (chop, resample, repeat), then builds the song around it.
//   MALLSOFT     ~70 BPM, a slowed VARISPEED loop of jazzy chords, huge plate, VAPOR high.
//   FUTURE FUNK  ~118 BPM, a SLICEd disco loop ducked by a four-on-the-floor kick.
//   CHIP-VAPOR   GB lead and SNES pad, a VARISPEED chip arpeggio loop slowed down.
namespace phos::content {

std::vector<std::string_view> template_names();
bool is_template(std::string_view name);
// nullopt for an unknown name. `name` is matched case-insensitively; "-" and " " are the same.
std::optional<model::Document> make_template(std::string_view name, std::uint32_t sample_rate);

} // namespace phos::content
