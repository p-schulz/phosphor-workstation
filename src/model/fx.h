#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Effects (PLAN.md §7.5, §7.8): the types, their parameters and one effect slot as the document
// stores it. Values are in display units (Hz, ms, bits, 0..1 amounts); the engine reads them as is.
namespace phos::model {

enum class FxType : std::uint8_t { Off, Chorus, Tape, Crush, Lofi, Plate, Delay, Duck };
inline constexpr int kFxTypes = 8;
inline constexpr int kFxParams = 5;

const char* fx_name(FxType t); // "OFF", "CHORUS", ...
// Upper-case name -> type; false if unknown.
bool fx_from_name(std::string_view name, FxType& out);

struct FxParamInfo {
    const char* name = ""; // "" = unused slot
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    float step = 0.01f;     // +/- step; MOD (big) steps are 10x
    const char* format = "%.2f";
    // Choice parameters: names for the integer values min..max (nullptr = numeric).
    const char* const* choices = nullptr;
};
const FxParamInfo& fx_param(FxType t, int index);

// Tempo-synced delay times (DELAY TIME choice) in quarter notes.
inline constexpr std::array<double, 8> kDelayBeats = {0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0};

struct FxSlot {
    FxType type = FxType::Off;
    std::array<float, kFxParams> p{};
    friend bool operator==(const FxSlot&, const FxSlot&) = default;
};

// A slot of `type` with every parameter at its default.
FxSlot make_fx(FxType type);
// Clamps the parameters into range (after loading or editing).
void clamp_fx(FxSlot& s);

} // namespace phos::model
