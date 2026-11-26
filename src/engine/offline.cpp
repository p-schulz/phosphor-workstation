#include "engine/offline.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "engine/snapshot_builder.h"
#include "seq/timing.h"

namespace phos::engine {

namespace {

// Plays `d` from the start (transport PLAY) through a private engine for `total` frames.
// Deterministic: no host clock, fixed 512-frame blocks, fresh engine and effect units.
void run_engine(const model::Document& d, std::uint32_t rate, std::size_t total, std::vector<float>& l,
                std::vector<float>& r) {
    auto eng = std::make_unique<Engine>();
    eng->prepare(rate, 2);
    SnapshotBuilder builder;
    builder.stretch_now(d, d.song.section().bpm10, rate);
    eng->publish(builder.build(d, rate));
    // The master level is a command in the live app (it follows the song); here it is sent once.
    eng->send(EngineCommand::set_param(EngineParam::MasterGainDb, d.song.master_db));
    eng->send(EngineCommand::transport(TransportAction::Play)); // handled after the snapshot swap
    l.assign(total, 0.0f);
    r.assign(total, 0.0f);
    std::vector<float> buf(2 * 512);
    for (std::size_t pos = 0; pos < total; pos += 512) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(512, total - pos));
        eng->render_offline(buf.data(), n, 2);
        for (std::uint32_t i = 0; i < n; ++i) {
            l[pos + i] = buf[2 * i];
            r[pos + i] = buf[2 * i + 1];
        }
    }
    eng->collect_garbage();
}

void prepare_doc(model::Song& s, bool master) {
    s.metronome = model::MetronomeMode::Off;
    s.count_in_bars = 0;
    if (!master) {
        s.master_fx = {};
        s.vapor = 0;
    }
}

} // namespace

model::SampleRef render_section(const model::Document& doc, const OfflineRender& opt) {
    model::Document d = doc; // samples are shared, not copied
    model::Song& s = d.song;
    s.current_section = std::clamp(opt.section, 0, model::kSections - 1);
    s.play_mode = model::PlayMode::Pattern;
    prepare_doc(s, opt.master);
    const model::Section& sec = s.section();
    const std::uint32_t rate = opt.sample_rate;
    const std::uint64_t pass = seq::frame_of_tick(s.pattern().length_ticks(), rate, sec.bpm10);
    const std::size_t skip = opt.seamless ? static_cast<std::size_t>(pass) : 0;
    const auto total = skip + static_cast<std::size_t>(pass * static_cast<std::uint64_t>(std::max(1, opt.passes)) +
                                                       static_cast<std::uint64_t>(opt.tail_s * rate));
    std::vector<float> l;
    std::vector<float> r;
    run_engine(d, rate, total, l, r);
    l.erase(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(skip));
    r.erase(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(skip));
    return model::make_sample(opt.name, "", rate, std::move(l), std::move(r));
}

std::uint64_t song_frames(const model::Song& s, std::uint32_t rate) {
    const auto pass = [&](int section) {
        const model::Section& sec = s.sections[static_cast<std::size_t>(section)];
        const auto& p = s.patterns[static_cast<std::size_t>(std::clamp(sec.pattern, 0, static_cast<int>(s.patterns.size()) - 1))];
        return seq::frame_of_tick(static_cast<std::uint64_t>(p.length_ticks()) * static_cast<std::uint64_t>(std::max(1, sec.repeats)),
                                  rate, std::clamp(sec.bpm10, 200, 3000));
    };
    if (s.play_mode == model::PlayMode::Pattern) {
        return pass(s.current_section);
    }
    std::uint64_t total = 0;
    for (const int sec : s.order) {
        total += pass(std::clamp(sec, 0, model::kSections - 1));
    }
    return total;
}

std::vector<int> stem_tracks(const model::Song& s) {
    // Patterns that play: the order in SONG mode, the current section in PATTERN mode.
    std::vector<bool> plays(s.patterns.size(), false);
    const auto mark = [&](int sec) {
        const int p = s.sections[static_cast<std::size_t>(std::clamp(sec, 0, model::kSections - 1))].pattern;
        if (p >= 0 && p < static_cast<int>(plays.size())) {
            plays[static_cast<std::size_t>(p)] = true;
        }
    };
    if (s.play_mode == model::PlayMode::Song) {
        for (const int sec : s.order) {
            mark(sec);
        }
    } else {
        mark(s.current_section);
    }
    std::vector<int> out;
    for (int t = 0; t < model::kTracks; ++t) {
        const auto& tr = s.tracks[static_cast<std::size_t>(t)];
        bool used = false;
        if (tr.type == model::TrackType::Notes) {
            for (std::size_t p = 0; p < s.patterns.size(); ++p) {
                used = used || (plays[p] && !s.patterns[p].lanes[static_cast<std::size_t>(t)].empty());
            }
        }
        for (const auto& l : s.loops) {
            used = used || (l.sample && l.track == t);
        }
        if (used && !tr.mute) {
            out.push_back(t);
        }
    }
    return out;
}

Render render_song(const model::Document& doc, const SongRender& opt) {
    model::Document d = doc;
    prepare_doc(d.song, opt.master);
    const auto total =
        static_cast<std::size_t>(song_frames(d.song, opt.sample_rate) + static_cast<std::uint64_t>(opt.tail_s * opt.sample_rate));
    Render out;
    run_engine(d, opt.sample_rate, total, out.left, out.right);
    if (!opt.stems) {
        return out;
    }
    for (const int t : stem_tracks(d.song)) {
        model::Document solo = d; // only track t audible: its sends and the master chain stay on
        for (int k = 0; k < model::kTracks; ++k) {
            auto& tr = solo.song.tracks[static_cast<std::size_t>(k)];
            tr.solo = false;
            tr.mute = k != t;
        }
        Stem stem;
        stem.track = t;
        run_engine(solo, opt.sample_rate, total, stem.left, stem.right);
        out.stems.push_back(std::move(stem));
    }
    return out;
}

} // namespace phos::engine
