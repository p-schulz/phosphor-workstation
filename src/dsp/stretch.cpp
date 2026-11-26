#include "dsp/stretch.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace phos::dsp {

StretchOutput stretch(const StretchInput& in, std::size_t out_frames, float semitones, bool loop) {
    StretchOutput out;
    const int channels = in.right ? 2 : 1;
    out.left.assign(out_frames, 0.0f);
    if (channels == 2) {
        out.right.assign(out_frames, 0.0f);
    }
    if (in.frames == 0 || out_frames == 0 || !in.left) {
        return out;
    }

    // Loops: pad with `pad` frames of the loop's end before and its start after, so the stretcher
    // sees the material it wraps into; then cut the middle part out.
    const double ratio = static_cast<double>(out_frames) / static_cast<double>(in.frames); // output / input
    const std::size_t pad = loop ? std::min(in.frames, static_cast<std::size_t>(in.sample_rate / 2)) : 0;
    const std::size_t in_total = in.frames + 2 * pad;
    std::array<std::vector<float>, 2> src;
    for (int c = 0; c < channels; ++c) {
        const float* p = c == 0 ? in.left : in.right;
        auto& v = src[static_cast<std::size_t>(c)];
        v.resize(in_total);
        for (std::size_t i = 0; i < in_total; ++i) {
            v[i] = p[(i + in.frames - pad % in.frames) % in.frames]; // loop: wraps, one-shot: pad = 0
        }
    }
    const std::size_t pre = static_cast<std::size_t>(static_cast<double>(pad) * ratio + 0.5);
    const std::size_t out_total = out_frames + 2 * pre;
    std::array<std::vector<float>, 2> dst;
    for (int c = 0; c < channels; ++c) {
        dst[static_cast<std::size_t>(c)].assign(out_total, 0.0f);
    }

    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault(channels, static_cast<float>(in.sample_rate));
    st.setTransposeSemitones(semitones);
    std::array<float*, 2> ins{src[0].data(), channels == 2 ? src[1].data() : nullptr};
    std::array<float*, 2> outs{dst[0].data(), channels == 2 ? dst[1].data() : nullptr};
    if (!st.exact(ins, static_cast<int>(in_total), outs, static_cast<int>(out_total))) {
        // Too short for the stretcher: plain linear resampling (pitch follows).
        for (int c = 0; c < channels; ++c) {
            const float* p = c == 0 ? in.left : in.right;
            auto& o = c == 0 ? out.left : out.right;
            for (std::size_t i = 0; i < out_frames; ++i) {
                const double x = static_cast<double>(i) / ratio;
                const auto i0 = static_cast<std::size_t>(x);
                const double f = x - static_cast<double>(i0);
                const float a = p[std::min(i0, in.frames - 1)];
                const float b = p[loop ? (i0 + 1) % in.frames : std::min(i0 + 1, in.frames - 1)];
                o[i] = a + static_cast<float>(f) * (b - a);
            }
        }
        return out;
    }
    // A stretched loop rarely holds whole cycles any more, so the jump from its end back to its start
    // would click. The stretcher's output just after the loop end continues the end seamlessly: the
    // loop start fades from that continuation into the real start (raised cosine, 20 ms).
    const std::size_t fade = loop ? std::min({pre, out_frames / 4, static_cast<std::size_t>(in.sample_rate / 50)}) : 0;
    for (int c = 0; c < channels; ++c) {
        auto& o = c == 0 ? out.left : out.right;
        const auto& d = dst[static_cast<std::size_t>(c)];
        std::copy_n(d.begin() + static_cast<std::ptrdiff_t>(pre), out_frames, o.begin());
        for (std::size_t i = 0; i < fade; ++i) {
            const float w = 0.5f - 0.5f * std::cos(3.14159265f * static_cast<float>(i) / static_cast<float>(fade));
            o[i] = w * o[i] + (1.0f - w) * d[pre + out_frames + i];
        }
    }
    return out;
}

} // namespace phos::dsp
