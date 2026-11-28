#include "seq/timing.h"

#include <cstdio>

namespace phos::seq {

std::uint32_t swing_tick(std::uint32_t tick, int grid_ticks, int percent) {
    if (percent <= 50 || grid_ticks <= 0) {
        return tick;
    }
    const auto g = static_cast<std::uint32_t>(grid_ticks);
    const std::uint32_t pair = 2 * g;
    const std::uint32_t base = tick / pair * pair;
    const std::uint32_t pos = tick - base;
    const std::uint32_t split = pair * static_cast<std::uint32_t>(percent) / 100; // where the off-beat lands
    std::uint32_t warped = 0;
    if (pos < g) {
        warped = pos * split / g;
    } else {
        warped = split + (pos - g) * (pair - split) / g;
    }
    return base + warped;
}

std::string bar_beat_tick(std::uint64_t tick, int sig_num, int sig_den) {
    const auto beat = static_cast<std::uint64_t>(beat_ticks(sig_den));
    const auto bar = static_cast<std::uint64_t>(sig_num) * beat;
    char buf[72];
    std::snprintf(buf, sizeof buf, "%03llu:%llu:%03llu", static_cast<unsigned long long>(tick / bar + 1),
                  static_cast<unsigned long long>(tick % bar / beat + 1), static_cast<unsigned long long>(tick % beat));
    return buf;
}

} // namespace phos::seq
