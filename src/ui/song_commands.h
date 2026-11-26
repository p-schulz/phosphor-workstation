#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ui/commands.h"
#include "ui/ui.h"

namespace phos::ui {

// TEMPO, SIG, SWING, TAP, METRO, MODE, QUANTIZE, TRACK. Returns the INFO reply, or nullopt when the
// verb is not one of these. `taps`: TAP history (seconds).
std::optional<std::string> run_song_command(Ui& ui, const Command& cmd, std::vector<double>& taps);

// M7 (ui/mix_commands.cpp): VAPOR, FX, SEND, RETURN, TRACKTYPE and LOOP <n> FROM|CLEAR|BARS|SYNC|QUANT.
std::optional<std::string> run_mix_command(Ui& ui, const Command& cmd);

} // namespace phos::ui
