#include "engine/wavetables.h"

#include <algorithm>
#include <complex>

#include "dsp/analysis.h"

namespace phos::engine {

std::shared_ptr<const WaveTables> build_tables(const model::WavetableData& data, const model::WavetableData* previous,
                                               const WaveTables* previous_tables) {
    auto t = std::make_shared<WaveTables>();
    constexpr int n = WaveTables::kPoints;
    t->segments = static_cast<int>(data.segments.size());
    t->cycles.resize(data.segments.size());
    t->data.assign(data.segments.size() * WaveTables::kLevels * WaveTables::kStride, 0.0f);
    std::vector<std::complex<double>> spec(n);
    std::vector<std::complex<double>> buf(n);
    for (int s = 0; s < t->segments; ++s) {
        const auto& seg = data.segments[static_cast<std::size_t>(s)];
        t->cycles[static_cast<std::size_t>(s)] = std::max(1, seg.cycles);
        if (previous && previous_tables && s < static_cast<int>(previous->segments.size()) && s < previous_tables->segments &&
            previous->segments[static_cast<std::size_t>(s)].wave == seg.wave) {
            std::copy_n(previous_tables->table(s, 0), WaveTables::kLevels * WaveTables::kStride,
                        t->data.data() + static_cast<std::size_t>(s) * WaveTables::kLevels * WaveTables::kStride);
            continue;
        }
        for (int i = 0; i < n; ++i) {
            spec[static_cast<std::size_t>(i)] = {seg.wave[static_cast<std::size_t>(i)], 0.0};
        }
        dsp::fft(spec);
        for (int l = 0; l < WaveTables::kLevels; ++l) {
            const int keep = 64 >> l;
            // Inverse FFT by conjugation: ifft(X) = conj(fft(conj(X))) / n. DC is dropped as well.
            for (int k = 0; k < n; ++k) {
                const int h = k <= n / 2 ? k : n - k;
                buf[static_cast<std::size_t>(k)] = h >= 1 && h <= keep ? std::conj(spec[static_cast<std::size_t>(k)]) : 0.0;
            }
            dsp::fft(buf);
            float* out = t->data.data() + (static_cast<std::size_t>(s) * WaveTables::kLevels + static_cast<std::size_t>(l)) * WaveTables::kStride;
            for (int i = 0; i < n; ++i) {
                out[i] = static_cast<float>(std::conj(buf[static_cast<std::size_t>(i)]).real() / n);
            }
            out[n] = out[0]; // guard sample
        }
    }
    return t;
}

} // namespace phos::engine
