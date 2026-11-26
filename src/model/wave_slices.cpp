#include "model/wave_slices.h"

#include <algorithm>
#include <cmath>

namespace phos::model::slices {

namespace {

Slices empty_slices() {
    Slices out;
    out.rows.assign(kSlices, std::array<float, kPoints>{});
    out.keyframe.assign(kSlices, false);
    return out;
}

// The segment after `s` as WaveVoice::next_segment picks it: the last loop segment goes back to the
// loop start; segments after the loop never play, so they just lead to the next one.
int next_segment(int s, int n, const WavetableParams& w) {
    const int loop_start = std::clamp(w.loop_start, 0, n - 1);
    const int loop_len = std::clamp(w.loop_length, 0, n - loop_start);
    if (loop_len > 0 && s == loop_start + loop_len - 1) {
        return loop_start;
    }
    return std::min(s + 1, n - 1);
}

} // namespace

Slices from_wavetable(const WavetableData& table, const WavetableParams& params) {
    Slices out = empty_slices();
    const int n = static_cast<int>(table.segments.size());
    if (n == 0) {
        return out;
    }
    std::vector<double> begin(static_cast<std::size_t>(n) + 1, 0.0); // in cycles
    for (int s = 0; s < n; ++s) {
        begin[static_cast<std::size_t>(s) + 1] =
            begin[static_cast<std::size_t>(s)] + std::max(1, table.segments[static_cast<std::size_t>(s)].cycles);
    }
    const double total = begin.back();
    int s = 0;
    for (int k = 0; k < kSlices; ++k) {
        const double t = total * k / kSlices;
        const double t_next = total * (k + 1) / kSlices;
        while (s + 1 < n && begin[static_cast<std::size_t>(s) + 1] <= t) {
            ++s;
        }
        const auto& a = table.segments[static_cast<std::size_t>(s)];
        const auto& b = table.segments[static_cast<std::size_t>(next_segment(s, n, params))];
        const double length = begin[static_cast<std::size_t>(s) + 1] - begin[static_cast<std::size_t>(s)];
        const auto frac = static_cast<float>(params.crossfade ? (t - begin[static_cast<std::size_t>(s)]) / length : 0.0);
        auto& row = out.rows[static_cast<std::size_t>(k)];
        for (int i = 0; i < kPoints; ++i) {
            const auto ix = static_cast<std::size_t>(i);
            row[ix] = a.wave[ix] + (b.wave[ix] - a.wave[ix]) * frac;
        }
        // A real segment starts within this slice (the first one at or after t).
        auto first = std::lower_bound(begin.begin(), begin.end() - 1, t);
        out.keyframe[static_cast<std::size_t>(k)] = first != begin.end() - 1 && *first < t_next;
    }
    return out;
}

Slices from_sample(const SampleData& sample, std::size_t start, std::size_t end, double period_frames,
                   SampleView view) {
    Slices out = empty_slices();
    end = std::min(end, sample.frames);
    start = std::min(start, end);
    const std::size_t len = end - start;
    if (len < 2 || sample.left.size() < sample.frames) {
        return out;
    }
    const float* l = sample.data(0);
    const float* r = sample.data(1);
    const bool stereo = sample.channels == 2 && sample.right.size() >= sample.frames;
    auto mono = [&](std::size_t i) { return stereo ? 0.5f * (l[i] + r[i]) : l[i]; };

    float peak = 0.0f;
    for (std::size_t i = start; i < end; ++i) {
        peak = std::max(peak, std::fabs(mono(i)));
    }
    const float scale = peak > 0.0f ? 1.0f / peak : 1.0f;

    if (view == SampleView::Cycle) {
        const double period = std::clamp(period_frames, 4.0, static_cast<double>(len - 1));
        const double last_start = static_cast<double>(end - 1) - period;
        // The rising zero crossing nearest to `at` within +-`reach` frames, or `at` if there is none.
        auto crossing_near = [&](double at, double reach) {
            const auto lo = static_cast<std::size_t>(std::max(static_cast<double>(start) + 1.0, at - reach));
            const auto hi = static_cast<std::size_t>(std::clamp(at + reach, static_cast<double>(start) + 1.0,
                                                                static_cast<double>(end - 1)));
            double best = at;
            double best_dist = reach + 1.0;
            for (std::size_t i = lo; i <= hi; ++i) {
                const float a = mono(i - 1);
                const float b = mono(i);
                if (a <= 0.0f && b > 0.0f) {
                    const double x = static_cast<double>(i - 1) + (-a) / (b - a);
                    if (std::fabs(x - at) < best_dist) {
                        best = x;
                        best_dist = std::fabs(x - at);
                    }
                }
            }
            return best;
        };
        // Follow the pitch cycle by cycle: each cycle starts at the crossing nearest to one period
        // after the previous one, so the same point of the wave stays at the left edge (also when
        // the root key is a little off or the pitch moves).
        std::vector<double> cycle;
        double at = std::min(crossing_near(static_cast<double>(start) + period / 2, period / 2), std::max(0.0, last_start));
        while (at <= last_start) {
            cycle.push_back(at);
            const double next = crossing_near(at + period, period / 4);
            at = next > at + period / 2 ? next : at + period;
        }
        if (cycle.empty()) {
            cycle.push_back(static_cast<double>(start));
        }
        // One cycle resampled to 128 points: from its start to the next cycle's (or one period).
        auto cycle_point = [&](std::size_t m, int j) {
            const double from = cycle[m];
            const double length = m + 1 < cycle.size() ? cycle[m + 1] - from : period;
            const double pos = std::min(from + length * j / kPoints, static_cast<double>(end - 1));
            const auto i0 = static_cast<std::size_t>(pos);
            const std::size_t i1 = std::min(i0 + 1, end - 1);
            const auto f = static_cast<float>(pos - static_cast<double>(i0));
            return mono(i0) + (mono(i1) - mono(i0)) * f;
        };
        // Slice k sits at cycle q = k / 127 of the way through; between two cycles it is their mix.
        for (int k = 0; k < kSlices; ++k) {
            const double q = static_cast<double>(cycle.size() - 1) * k / (kSlices - 1);
            const auto m = static_cast<std::size_t>(q);
            const std::size_t m1 = std::min(m + 1, cycle.size() - 1);
            const auto f = static_cast<float>(q - static_cast<double>(m));
            auto& row = out.rows[static_cast<std::size_t>(k)];
            for (int j = 0; j < kPoints; ++j) {
                const float v = cycle_point(m, j) + (cycle_point(m1, j) - cycle_point(m, j)) * f;
                row[static_cast<std::size_t>(j)] = std::clamp(v * scale, -1.0f, 1.0f);
            }
        }
        return out;
    }

    for (int k = 0; k < kSlices; ++k) {
        auto& row = out.rows[static_cast<std::size_t>(k)];
        for (int j = 0; j < kPoints; ++j) {
            // Bin j of slice k: frames [a, b) of the range, at least one.
            const std::size_t bin = static_cast<std::size_t>(k) * kPoints + static_cast<std::size_t>(j);
            const std::size_t a = start + len * bin / (kSlices * kPoints);
            const std::size_t b = std::max(a + 1, start + len * (bin + 1) / (kSlices * kPoints));
            float p = 0.0f; // the value furthest from zero, with its sign
            for (std::size_t i = a; i < std::min(b, end); ++i) {
                const float v = mono(i);
                p = std::fabs(v) > std::fabs(p) ? v : p;
            }
            row[static_cast<std::size_t>(j)] = std::clamp(p * scale, -1.0f, 1.0f);
        }
    }
    return out;
}

} // namespace phos::model::slices
