#include "model/fx.h"

#include <algorithm>
#include <cmath>

namespace phos::model {

namespace {

constexpr const char* kChorusModes[] = {"I", "II", "I+II"};
constexpr const char* kOffOn[] = {"OFF", "ON"};
constexpr const char* kDelayTimes[] = {"1/16", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2"};
constexpr const char* kGateModes[] = {"GATED", "NONLIN", "REVERSE"};
constexpr const char* kTracks[] = {"1", "2", "3", "4", "5", "6", "7", "8", "A", "B", "C", "D", "E", "F", "G", "H"};

using P = FxParamInfo;
constexpr P kNone{};
constexpr P kMix{"MIX", 0.0f, 1.0f, 0.5f, 0.05f, "%.2f"};

// Five parameters per type (unused ones empty). Defaults sound useful straight away.
constexpr std::array<std::array<P, kFxParams>, kFxTypes> kParams = {{
    {kNone, kNone, kNone, kNone, kNone}, // OFF
    {{
        {"MODE", 0, 2, 0, 1, "%.0f", kChorusModes},
        {"DEPTH", 0, 1, 1, 0.05f},
        {"TONE", 1000, 20000, 8000, 250, "%.0f"},
        kNone,
        kMix,
    }},
    {{
        {"WOW", 0, 1, 0.4f, 0.05f},
        {"FLUTTER", 0, 1, 0.3f, 0.05f},
        {"DRIVE", 0, 1, 0.3f, 0.05f},
        {"HISS", 0, 1, 0.2f, 0.05f},
        {"MIX", 0, 1, 1.0f, 0.05f},
    }},
    {{
        {"BITS", 1, 16, 8, 1, "%.0f"},
        {"RATE", 500, 48000, 11025, 500, "%.0f"},
        kNone,
        kNone,
        {"MIX", 0, 1, 1.0f, 0.05f},
    }},
    {{
        {"CUTOFF", 100, 20000, 3000, 100, "%.0f"},
        {"RESO", 0, 1, 0.3f, 0.05f},
        {"DRIVE", 0, 1, 0.0f, 0.05f},
        kNone,
        {"MIX", 0, 1, 1.0f, 0.05f},
    }},
    {{
        {"DECAY", 0, 0.98f, 0.7f, 0.02f},
        {"DAMP", 0, 1, 0.4f, 0.05f},
        {"PREDLY", 0, 200, 20, 5, "%.0f"},
        {"WIDTH", 0, 1, 1.0f, 0.05f},
        {"MIX", 0, 1, 0.3f, 0.05f},
    }},
    {{
        {"TIME", 0, 7, 3, 1, "%.0f", kDelayTimes},
        {"FEEDBK", 0, 0.95f, 0.45f, 0.05f},
        {"LP", 500, 20000, 5000, 250, "%.0f"},
        {"PING", 0, 1, 1, 1, "%.0f", kOffOn},
        {"MIX", 0, 1, 0.3f, 0.05f},
    }},
    {{
        {"SOURCE", 0, 15, 0, 1, "%.0f", kTracks},
        {"DEPTH", 0, 1, 0.6f, 0.05f},
        {"RELEASE", 20, 1000, 180, 10, "%.0f"},
        {"THRESH", -60, 0, -30, 1, "%.0f"},
        kNone,
    }},
    {{
        {"MODE", 0, 2, 0, 1, "%.0f", kGateModes},
        {"TIME", 80, 800, 300, 10, "%.0f"},
        {"THRESH", -60, 0, -24, 1, "%.0f"},
        {"TONE", 1000, 20000, 9000, 250, "%.0f"},
        {"MIX", 0, 1, 0.4f, 0.05f},
    }},
}};

constexpr const char* kNames[] = {"OFF", "CHORUS", "TAPE", "CRUSH", "LOFI", "PLATE", "DELAY", "DUCK", "GATE"};

} // namespace

const char* fx_name(FxType t) { return kNames[std::min<std::size_t>(static_cast<std::size_t>(t), kFxTypes - 1)]; }

bool fx_from_name(std::string_view name, FxType& out) {
    for (int i = 0; i < kFxTypes; ++i) {
        if (name == kNames[i]) {
            out = static_cast<FxType>(i);
            return true;
        }
    }
    return false;
}

const FxParamInfo& fx_param(FxType t, int index) {
    if (index < 0 || index >= kFxParams) {
        return kNone;
    }
    return kParams[std::min<std::size_t>(static_cast<std::size_t>(t), kFxTypes - 1)][static_cast<std::size_t>(index)];
}

FxSlot make_fx(FxType type) {
    FxSlot s;
    s.type = type;
    for (int i = 0; i < kFxParams; ++i) {
        s.p[static_cast<std::size_t>(i)] = fx_param(type, i).def;
    }
    return s;
}

void clamp_fx(FxSlot& s) {
    for (int i = 0; i < kFxParams; ++i) {
        const FxParamInfo& info = fx_param(s.type, i);
        float& v = s.p[static_cast<std::size_t>(i)];
        if (!std::isfinite(v)) {
            v = info.def;
        }
        v = std::clamp(v, info.min, info.max);
        if (info.choices) {
            v = std::round(v);
        }
    }
}

} // namespace phos::model
