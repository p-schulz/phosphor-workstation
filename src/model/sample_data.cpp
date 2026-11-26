#include "model/sample_data.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phos::model {

namespace {

void build_overview(SampleData& s) {
    Overview& o = s.overview;
    o.min.assign(Overview::kBins, 0.0f);
    o.max.assign(Overview::kBins, 0.0f);
    o.rms.assign(Overview::kBins, 0.0f);
    if (s.frames == 0) {
        return;
    }
    for (std::size_t b = 0; b < Overview::kBins; ++b) {
        const std::size_t from = b * s.frames / Overview::kBins;
        const std::size_t to = std::max(from + 1, (b + 1) * s.frames / Overview::kBins);
        float lo = 1e9f;
        float hi = -1e9f;
        double sq = 0.0;
        for (std::size_t i = from; i < to && i < s.frames; ++i) {
            const float v = s.channels == 2 ? 0.5f * (s.left[i] + s.right[i]) : s.left[i];
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sq += static_cast<double>(v) * v;
        }
        o.min[b] = lo;
        o.max[b] = hi;
        o.rms[b] = static_cast<float>(std::sqrt(sq / static_cast<double>(to - from)));
    }
}

// Copy of `s` with both channels passed through `fn(std::vector<float>&)`.
template <typename Fn> SampleRef transformed(const SampleData& s, Fn&& fn) {
    std::vector<float> l = s.left;
    std::vector<float> r = s.right;
    fn(l);
    if (!r.empty()) {
        fn(r);
    }
    return make_sample(s.name, s.source_path, s.sample_rate, std::move(l), std::move(r));
}

void clamp_range(const SampleData& s, std::size_t& start, std::size_t& end) {
    end = std::min(end, s.frames);
    start = std::min(start, end);
}

} // namespace

float SampleData::peak() const {
    float p = 0.0f;
    for (const float v : left) {
        p = std::max(p, std::fabs(v));
    }
    for (const float v : right) {
        p = std::max(p, std::fabs(v));
    }
    return p;
}

SampleRef make_sample(std::string name, std::string source_path, std::uint32_t sample_rate, std::vector<float> left,
                      std::vector<float> right) {
    auto s = std::make_shared<SampleData>();
    s->name = std::move(name);
    s->source_path = std::move(source_path);
    s->sample_rate = sample_rate;
    s->frames = left.size();
    s->channels = right.empty() ? 1 : 2;
    if (!right.empty()) {
        right.resize(left.size());
    }
    s->left = std::move(left);
    s->right = std::move(right);
    build_overview(*s);
    return s;
}

namespace edit {

SampleRef trim(const SampleData& s, std::size_t start, std::size_t end) {
    clamp_range(s, start, end);
    return transformed(s, [&](std::vector<float>& ch) {
        ch.erase(ch.begin() + static_cast<std::ptrdiff_t>(end), ch.end());
        ch.erase(ch.begin(), ch.begin() + static_cast<std::ptrdiff_t>(start));
    });
}

SampleRef fade_in(const SampleData& s, std::size_t start, std::size_t end) {
    clamp_range(s, start, end);
    const double len = static_cast<double>(std::max<std::size_t>(1, end - start));
    return transformed(s, [&](std::vector<float>& ch) {
        for (std::size_t i = 0; i < start; ++i) {
            ch[i] = 0.0f; // silence before the fade starts
        }
        for (std::size_t i = start; i < end; ++i) {
            ch[i] *= static_cast<float>(static_cast<double>(i - start) / len);
        }
    });
}

SampleRef fade_out(const SampleData& s, std::size_t start, std::size_t end) {
    clamp_range(s, start, end);
    const double len = static_cast<double>(std::max<std::size_t>(1, end - start));
    return transformed(s, [&](std::vector<float>& ch) {
        for (std::size_t i = start; i < end; ++i) {
            ch[i] *= static_cast<float>(1.0 - static_cast<double>(i - start + 1) / len);
        }
        for (std::size_t i = end; i < ch.size(); ++i) {
            ch[i] = 0.0f; // silence after the fade ends
        }
    });
}

SampleRef normalize(const SampleData& s, float peak_db) {
    const float p = s.peak();
    const float gain = p > 0.0f ? std::pow(10.0f, peak_db / 20.0f) / p : 1.0f;
    return transformed(s, [&](std::vector<float>& ch) {
        for (float& v : ch) {
            v *= gain;
        }
    });
}

SampleRef reverse(const SampleData& s, std::size_t start, std::size_t end) {
    clamp_range(s, start, end);
    return transformed(s, [&](std::vector<float>& ch) {
        std::reverse(ch.begin() + static_cast<std::ptrdiff_t>(start), ch.begin() + static_cast<std::ptrdiff_t>(end));
    });
}

SampleRef crossfade_loop(const SampleData& s, std::size_t loop_start, std::size_t loop_end, std::size_t length) {
    loop_end = std::min(loop_end, s.frames);
    length = std::min({length, loop_start, loop_end > loop_start ? loop_end - loop_start : 0});
    return transformed(s, [&](std::vector<float>& ch) {
        const std::vector<float> orig = ch;
        for (std::size_t i = 0; i < length; ++i) {
            // Towards loop_end the tail turns into the material just before loop_start, which is
            // what originally led into loop_start: the wrap point becomes continuous.
            const double t = static_cast<double>(i + 1) / static_cast<double>(length);
            const double fade_out = std::cos(t * std::numbers::pi / 2.0);
            const double fade_in = std::sin(t * std::numbers::pi / 2.0);
            const std::size_t at = loop_end - length + i;
            const std::size_t from = loop_start - length + i;
            ch[at] = static_cast<float>(orig[at] * fade_out + orig[from] * fade_in);
        }
    });
}

} // namespace edit

} // namespace phos::model
