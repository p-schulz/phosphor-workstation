#include "dsp/yin.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace phos::dsp {

PitchEstimate yin(std::span<const float> x, double sample_rate) {
    constexpr double kMinHz = 30.0;
    constexpr double kMaxHz = 2000.0;
    constexpr double kThreshold = 0.15;
    const auto max_lag = static_cast<std::size_t>(sample_rate / kMinHz);
    const auto min_lag = std::max<std::size_t>(2, static_cast<std::size_t>(sample_rate / kMaxHz));
    if (x.size() < 2 * max_lag + 2) {
        return {};
    }
    const std::size_t window = x.size() - max_lag - 1;

    // Step 2: difference function d(tau).
    std::vector<double> d(max_lag + 2, 0.0);
    for (std::size_t tau = 1; tau <= max_lag + 1; ++tau) {
        double sum = 0.0;
        for (std::size_t i = 0; i < window; ++i) {
            const double diff = static_cast<double>(x[i]) - x[i + tau];
            sum += diff * diff;
        }
        d[tau] = sum;
    }
    // Step 3: cumulative mean normalised difference d'(tau).
    std::vector<double> dn(max_lag + 2, 1.0);
    double running = 0.0;
    for (std::size_t tau = 1; tau <= max_lag + 1; ++tau) {
        running += d[tau];
        dn[tau] = running > 0.0 ? d[tau] * static_cast<double>(tau) / running : 1.0;
    }
    // Step 4: first dip below the threshold (then its local minimum); else the global minimum.
    std::size_t best = 0;
    for (std::size_t tau = min_lag; tau <= max_lag; ++tau) {
        if (dn[tau] < kThreshold) {
            while (tau + 1 <= max_lag && dn[tau + 1] < dn[tau]) {
                ++tau;
            }
            best = tau;
            break;
        }
    }
    if (best == 0) {
        best = static_cast<std::size_t>(std::min_element(dn.begin() + static_cast<std::ptrdiff_t>(min_lag),
                                                         dn.begin() + static_cast<std::ptrdiff_t>(max_lag) + 1) -
                                        dn.begin());
    }
    // Step 5: parabolic interpolation around the minimum.
    double lag = static_cast<double>(best);
    if (best > 1 && best + 1 <= max_lag + 1) {
        const double a = dn[best - 1];
        const double b = dn[best];
        const double c = dn[best + 1];
        const double denom = a - 2.0 * b + c;
        if (std::fabs(denom) > 1e-12) {
            lag += 0.5 * (a - c) / denom;
        }
    }
    return {sample_rate / lag, std::clamp(1.0 - dn[best], 0.0, 1.0)};
}

} // namespace phos::dsp
