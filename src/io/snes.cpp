#include "io/snes.h"

#include <algorithm>
#include <cmath>

#include "io/import.h"
#include "model/workspace.h"

namespace phos::io {

model::BrrRef encode_for_snes(const model::SampleData& s, const model::SamplerParams& sampler,
                              const model::SnesParams& snes) {
    std::vector<float> mono(s.frames);
    for (std::size_t i = 0; i < s.frames; ++i) {
        mono[i] = s.channels == 2 ? 0.5f * (s.left[i] + s.right[i]) : s.left[i];
    }
    const int requested = std::clamp(snes.source_rate, 8000, 32000);
    double rate = requested;
    const bool loop = sampler.loop != model::LoopMode::Off && sampler.loop_end > sampler.loop_start + 16;
    double ratio = rate / s.sample_rate;
    if (loop && snes.auto_align) {
        // Loop length at the source rate, rounded to whole blocks: scale the rate to fit.
        const double len = static_cast<double>(sampler.loop_end - sampler.loop_start) * ratio;
        const double aligned = std::max(16.0, std::round(len / 16.0) * 16.0);
        rate *= aligned / len;
        ratio = rate / s.sample_rate;
    }
    std::vector<float> res = resample(mono, s.sample_rate, rate);
    std::vector<std::int16_t> pcm(res.size());
    for (std::size_t i = 0; i < res.size(); ++i) {
        pcm[i] = static_cast<std::int16_t>(std::clamp(std::lround(res[i] * 32767.0f), -32768L, 32767L));
    }
    int loop_start = -1;
    if (loop) {
        loop_start = static_cast<int>(std::lround(static_cast<double>(sampler.loop_start) * ratio));
        // The loop end becomes the sample end: BRR loops from the last block back to loop_start.
        const auto end = static_cast<std::size_t>(std::lround(static_cast<double>(sampler.loop_end) * ratio));
        const auto start_block = static_cast<std::size_t>(loop_start / 16);
        const std::size_t len_blocks = std::max<std::size_t>(1, (end - start_block * 16 + 8) / 16);
        pcm.resize(std::min(pcm.size(), (start_block + len_blocks) * 16));
    }
    auto brr = std::make_shared<model::BrrSample>(model::brr_encode(pcm, loop_start, static_cast<int>(std::lround(rate))));
    return brr;
}

std::size_t aram_bytes(const model::Document& doc) {
    std::size_t total = model::kAramDriverReserve + model::snes_echo_bytes(doc.song.snes_echo.delay);
    for (const auto& inst : doc.instruments) {
        if (inst.kind == model::InstrumentKind::Snes && inst.brr) {
            total += inst.brr->data.size();
        }
    }
    return total;
}

} // namespace phos::io
