#include "model/song.h"

#include <algorithm>
#include <cmath>

namespace phos::model {

int grid_ticks(Grid g) {
    switch (g) {
    case Grid::Quarter:
        return kPpq;
    case Grid::Eighth:
        return kPpq / 2;
    case Grid::Sixteenth:
        return kPpq / 4;
    case Grid::ThirtySecond:
        return kPpq / 8;
    case Grid::SixtyFourth:
        return kPpq / 16;
    case Grid::EighthT:
        return kPpq / 3;
    case Grid::SixteenthT:
        return kPpq / 6;
    case Grid::ThirtySecondT:
        return kPpq / 12;
    }
    return kPpq / 4;
}

const char* grid_name(Grid g) {
    switch (g) {
    case Grid::Quarter:
        return "1/4";
    case Grid::Eighth:
        return "1/8";
    case Grid::Sixteenth:
        return "1/16";
    case Grid::ThirtySecond:
        return "1/32";
    case Grid::SixtyFourth:
        return "1/64";
    case Grid::EighthT:
        return "1/8T";
    case Grid::SixteenthT:
        return "1/16T";
    case Grid::ThirtySecondT:
        return "1/32T";
    }
    return "1/16";
}

const char* midi_dest_name(MidiDest d) {
    switch (d) {
    case MidiDest::Internal:
        return "INT";
    case MidiDest::External:
        return "EXT";
    case MidiDest::Both:
        return "BOTH";
    }
    return "INT";
}

const AutomationLane* Pattern::lane(int track, int cc) const {
    for (const auto& l : automation) {
        if (l.track == track && l.cc == cc) {
            return &l;
        }
    }
    return nullptr;
}

AutomationLane& Pattern::lane_for(int track, int cc) {
    for (auto& l : automation) {
        if (l.track == track && l.cc == cc) {
            if (static_cast<int>(l.values.size()) < steps) {
                l.values.resize(static_cast<std::size_t>(steps), -1);
            }
            return l;
        }
    }
    automation.push_back(AutomationLane{track, cc, std::vector<std::int16_t>(static_cast<std::size_t>(steps), -1)});
    return automation.back();
}

const char* loop_sync_name(LoopSync s) {
    switch (s) {
    case LoopSync::Varispeed:
        return "VARI";
    case LoopSync::Stretch:
        return "STRETCH";
    case LoopSync::Slice:
        return "SLICE";
    }
    return "VARI";
}

const char* launch_quantize_name(LaunchQuantize q) {
    switch (q) {
    case LaunchQuantize::Immediate:
        return "NOW";
    case LaunchQuantize::Beat:
        return "BEAT";
    case LaunchQuantize::Bar:
        return "BAR";
    }
    return "BAR";
}

int detect_bars(double seconds, double bpm) {
    if (seconds <= 0.0 || bpm <= 0.0) {
        return 1;
    }
    const double bars = seconds * bpm / 240.0; // 4/4 bars at bpm
    const int exp = static_cast<int>(std::lround(std::log2(std::max(bars, 1e-6))));
    return 1 << std::clamp(exp, 0, 6);
}

char track_label(int track) { return static_cast<char>(track < 8 ? '1' + track : 'A' + (track - 8)); }

Song::Song() {
    for (int t = 0; t < kTracks; ++t) {
        tracks[static_cast<std::size_t>(t)].name = std::string("TRACK ") + track_label(t);
        tracks[static_cast<std::size_t>(t)].instrument = t; // track n plays instrument n by default
        tracks[static_cast<std::size_t>(t)].out_channel = t;
    }
    for (int i = 0; i < kLoopSlots; ++i) {
        loops[static_cast<std::size_t>(i)].track = 8 + i; // slots 1..8 play on tracks A..H
    }
    returns = {make_fx(FxType::Plate), make_fx(FxType::Delay)};
    for (auto& r : returns) {
        r.p[4] = 1.0f; // a return is all wet; its level is return_level
    }
}

void insert_note(std::vector<Note>& lane, const Note& n) {
    const auto it = std::lower_bound(lane.begin(), lane.end(), n,
                                     [](const Note& a, const Note& b) { return a.tick < b.tick; });
    for (auto j = it; j != lane.end() && j->tick == n.tick; ++j) {
        if (j->note == n.note) {
            *j = n;
            return;
        }
    }
    // After existing notes at the same tick: stable order of entry.
    auto pos = it;
    while (pos != lane.end() && pos->tick == n.tick) {
        ++pos;
    }
    lane.insert(pos, n);
}

int remove_notes(std::vector<Note>& lane, std::uint32_t from, std::uint32_t to) {
    const auto before = lane.size();
    lane.erase(std::remove_if(lane.begin(), lane.end(), [&](const Note& n) { return n.tick >= from && n.tick < to; }),
               lane.end());
    return static_cast<int>(before - lane.size());
}

} // namespace phos::model
