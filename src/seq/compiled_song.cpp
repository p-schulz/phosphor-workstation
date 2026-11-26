#include "seq/compiled_song.h"

#include <algorithm>
#include <cmath>

#include "seq/timing.h"

namespace phos::seq {

CompiledSong::CompiledSong() {
    patterns.push_back(SeqPattern{});
    for (int t = 0; t < model::kTracks; ++t) {
        tracks[static_cast<std::size_t>(t)].instrument = t;
    }
}

CompiledSong compile(const model::Song& song) {
    CompiledSong c;
    c.patterns.clear();
    const int swing_grid = model::grid_ticks(song.swing_grid);
    for (const auto& p : song.patterns) {
        SeqPattern sp;
        sp.length_ticks = std::max<std::uint32_t>(1, p.length_ticks());
        sp.begin = static_cast<std::uint32_t>(c.notes.size());
        for (int t = 0; t < model::kTracks; ++t) {
            const bool swung = song.tracks[static_cast<std::size_t>(t)].swing;
            for (const auto& n : p.lanes[static_cast<std::size_t>(t)]) {
                if (n.tick >= sp.length_ticks) {
                    continue; // beyond a shortened pattern: kept in the document, not played
                }
                SeqNote s;
                s.tick = swung ? swing_tick(n.tick, swing_grid, song.swing) : n.tick;
                const std::uint32_t off = std::min(n.tick + std::max<std::uint32_t>(1, n.length), sp.length_ticks);
                s.off_tick = std::max(s.tick + 1, swung ? swing_tick(off, swing_grid, song.swing) : off);
                s.note = n.note;
                s.velocity = n.velocity;
                s.track = static_cast<std::uint8_t>(t);
                c.notes.push_back(s);
            }
        }
        std::stable_sort(c.notes.begin() + sp.begin, c.notes.end(),
                         [](const SeqNote& a, const SeqNote& b) { return a.tick < b.tick; });
        sp.end = static_cast<std::uint32_t>(c.notes.size());
        sp.cc_begin = static_cast<std::uint32_t>(c.ccs.size());
        const std::uint32_t step = p.step_ticks();
        for (const auto& lane : p.automation) {
            const bool swung = song.tracks[static_cast<std::size_t>(std::clamp(lane.track, 0, model::kTracks - 1))].swing;
            const std::size_t n = std::min(lane.values.size(), static_cast<std::size_t>(std::max(0, p.steps)));
            for (std::size_t k = 0; k < n; ++k) {
                if (lane.values[k] < 0) {
                    continue;
                }
                const auto tick = static_cast<std::uint32_t>(k) * step;
                c.ccs.push_back(SeqCc{swung ? swing_tick(tick, swing_grid, song.swing) : tick,
                                      static_cast<std::uint8_t>(lane.cc & 0x7F), static_cast<std::uint8_t>(lane.values[k] & 0x7F),
                                      static_cast<std::uint8_t>(std::clamp(lane.track, 0, model::kTracks - 1))});
            }
        }
        std::stable_sort(c.ccs.begin() + sp.cc_begin, c.ccs.end(),
                         [](const SeqCc& a, const SeqCc& b) { return a.tick < b.tick; });
        sp.cc_end = static_cast<std::uint32_t>(c.ccs.size());
        c.patterns.push_back(sp);
    }
    if (c.patterns.empty()) {
        c.patterns.push_back(SeqPattern{});
    }
    for (int i = 0; i < model::kSections; ++i) {
        const auto& s = song.sections[static_cast<std::size_t>(i)];
        c.sections[static_cast<std::size_t>(i)] =
            SeqSection{std::clamp(s.pattern, 0, static_cast<int>(c.patterns.size()) - 1), std::max(1, s.repeats),
                       std::clamp(s.bpm10, 200, 3000), std::clamp(s.sig_num, 1, 16), s.sig_den};
    }
    c.order.clear();
    for (const int sec : song.order) {
        if (sec >= 0 && sec < model::kSections) {
            c.order.push_back(sec);
        }
    }
    if (c.order.empty()) {
        c.order.push_back(0);
    }
    c.play_mode = song.play_mode;
    c.current_section = std::clamp(song.current_section, 0, model::kSections - 1);
    c.metronome = song.metronome;
    c.count_in_bars = std::clamp(song.count_in_bars, 0, 2);
    c.record_offset_ms = song.record_offset_ms;
    const bool any_solo = std::any_of(song.tracks.begin(), song.tracks.end(), [](const model::Track& t) { return t.solo; });
    for (int t = 0; t < model::kTracks; ++t) {
        const auto& tr = song.tracks[static_cast<std::size_t>(t)];
        const bool audible = !tr.mute && (!any_solo || tr.solo);
        TrackBlock& b = c.tracks[static_cast<std::size_t>(t)];
        b.instrument = std::clamp(tr.instrument, 0, 15);
        b.gain = audible ? std::pow(10.0f, tr.volume_db / 20.0f) : 0.0f;
        b.pan = tr.pan;
        b.type = tr.type;
        b.inserts = tr.inserts;
        b.send_a = std::clamp(tr.send_a, 0.0f, 1.0f);
        b.send_b = std::clamp(tr.send_b, 0.0f, 1.0f);
        b.dest = tr.dest;
        b.out_channel = std::clamp(tr.out_channel, 0, 15);
        b.program = std::clamp(tr.program, -1, 127);
    }
    c.current_track = std::clamp(song.current_track, 0, model::kTracks - 1);
    c.snes_echo = song.snes_echo;
    c.strict_cmi = song.strict_cmi;
    c.strict_gb = song.strict_gb;
    c.strict_snes = song.strict_snes;
    c.returns = song.returns;
    c.return_level = song.return_level;
    c.master_fx = song.master_fx;
    c.vapor = std::clamp(song.vapor, 0, model::kVaporMax);
    return c;
}

} // namespace phos::seq
