#include "io/smf.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

#include "seq/compiled_song.h"
#include "seq/timing.h"

namespace phos::io {

namespace {

// ---------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------

struct Event {
    std::uint64_t tick = 0;
    int order = 0; // at equal ticks: meta first, note-offs before note-ons
    std::vector<std::uint8_t> bytes;
};

void put_vlq(std::vector<std::uint8_t>& out, std::uint64_t v) {
    std::uint8_t buf[10];
    int n = 0;
    buf[n++] = static_cast<std::uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) {
        buf[n++] = static_cast<std::uint8_t>(0x80 | (v & 0x7F));
    }
    while (n > 0) {
        out.push_back(buf[--n]);
    }
}

void put_be(std::vector<std::uint8_t>& out, std::uint32_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

std::vector<std::uint8_t> meta(std::uint8_t type, const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> m = {0xFF, type};
    put_vlq(m, data.size());
    m.insert(m.end(), data.begin(), data.end());
    return m;
}

std::vector<std::uint8_t> text(const std::string& s) { return {s.begin(), s.end()}; }

void put_track(std::vector<std::uint8_t>& out, std::vector<Event> events, std::uint64_t end_tick) {
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    std::vector<std::uint8_t> body;
    std::uint64_t at = 0;
    for (const Event& e : events) {
        put_vlq(body, e.tick - at);
        at = e.tick;
        body.insert(body.end(), e.bytes.begin(), e.bytes.end());
    }
    put_vlq(body, end_tick > at ? end_tick - at : 0);
    body.insert(body.end(), {0xFF, 0x2F, 0x00}); // end of track
    out.insert(out.end(), {'M', 'T', 'r', 'k'});
    put_be(out, static_cast<std::uint32_t>(body.size()), 4);
    out.insert(out.end(), body.begin(), body.end());
}

int log2_den(int den) {
    int n = 0;
    while ((1 << n) < den) {
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------------

struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t p = 0;
    bool ok = true;
    std::uint8_t u8() {
        if (p >= b.size()) {
            ok = false;
            return 0;
        }
        return b[p++];
    }
    std::uint32_t be(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            v = (v << 8) | u8();
        }
        return v;
    }
    std::uint64_t vlq() {
        std::uint64_t v = 0;
        for (int i = 0; i < 5; ++i) {
            const std::uint8_t c = u8();
            v = (v << 7) | (c & 0x7F);
            if ((c & 0x80) == 0) {
                return v;
            }
        }
        ok = false;
        return v;
    }
};

struct RawNote {
    std::uint64_t tick = 0;
    std::uint64_t length = 0;
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
};

struct RawCc {
    std::uint64_t tick = 0;
    std::uint8_t cc = 0;
    std::uint8_t value = 0;
};

// Controllers that become automation lanes (the ones PHOSPHOR also uses inside, plus cutoff).
bool imported_cc(int cc) { return cc == 1 || cc == 7 || cc == 10 || cc == 11 || cc == 74; }

struct RawTrack {
    std::string name;
    std::map<int, std::vector<RawNote>> by_channel;
    std::map<int, std::vector<RawCc>> ccs; // by channel
    std::map<int, int> program;            // channel -> first program change
};

} // namespace

std::vector<std::uint8_t> export_smf(const model::Document& doc, const std::string& title) {
    const model::Song& song = doc.song;
    const seq::CompiledSong c = seq::compile(song);
    // The sections as they play, with their start tick.
    std::vector<std::pair<int, std::uint64_t>> passes; // section, start tick
    std::uint64_t tick = 0;
    const auto add_section = [&](int sec) {
        const auto& s = c.sections[static_cast<std::size_t>(sec)];
        const auto len = c.patterns[static_cast<std::size_t>(s.pattern)].length_ticks;
        for (int r = 0; r < s.repeats; ++r) {
            passes.emplace_back(sec, tick);
            tick += len;
        }
    };
    if (song.play_mode == model::PlayMode::Song) {
        for (const int sec : c.order) {
            add_section(sec);
        }
    } else {
        add_section(c.current_section);
    }
    const std::uint64_t end = tick;

    std::vector<std::uint8_t> out = {'M', 'T', 'h', 'd'};
    put_be(out, 6, 4);
    put_be(out, 1, 2); // type 1
    std::vector<int> tracks;
    for (int t = 0; t < model::kTracks; ++t) {
        if (song.tracks[static_cast<std::size_t>(t)].type != model::TrackType::Notes) {
            continue;
        }
        for (const auto& [sec, start] : passes) {
            const auto& pat = c.patterns[static_cast<std::size_t>(c.sections[static_cast<std::size_t>(sec)].pattern)];
            for (std::uint32_t i = pat.begin; i < pat.end; ++i) {
                if (c.notes[i].track == t) {
                    tracks.push_back(t);
                    break;
                }
            }
            if (!tracks.empty() && tracks.back() == t) {
                break;
            }
        }
    }
    put_be(out, static_cast<std::uint32_t>(1 + tracks.size()), 2);
    put_be(out, kSmfPpq, 2);

    // Conductor track: name, tempo and signature wherever they change.
    std::vector<Event> conductor;
    conductor.push_back(Event{0, 0, meta(0x03, text(title))});
    int last_bpm10 = -1;
    int last_num = -1;
    int last_den = -1;
    for (const auto& [sec, start] : passes) {
        const auto& s = c.sections[static_cast<std::size_t>(sec)];
        if (s.bpm10 != last_bpm10) {
            last_bpm10 = s.bpm10;
            const auto us = static_cast<std::uint32_t>(std::llround(600000000.0 / s.bpm10)); // 60e6 / bpm
            std::vector<std::uint8_t> d;
            put_be(d, us, 3);
            conductor.push_back(Event{start, 1, meta(0x51, d)});
        }
        if (s.sig_num != last_num || s.sig_den != last_den) {
            last_num = s.sig_num;
            last_den = s.sig_den;
            conductor.push_back(Event{start, 1,
                                      meta(0x58, {static_cast<std::uint8_t>(s.sig_num),
                                                  static_cast<std::uint8_t>(log2_den(s.sig_den)), 24, 8})});
        }
    }
    put_track(out, conductor, end);

    for (const int t : tracks) {
        const auto& tr = song.tracks[static_cast<std::size_t>(t)];
        const auto ch = static_cast<std::uint8_t>(tr.out_channel & 0x0F);
        std::vector<Event> ev;
        ev.push_back(Event{0, 0, meta(0x03, text(tr.name))});
        if (tr.program >= 0) {
            ev.push_back(Event{0, 1, {static_cast<std::uint8_t>(0xC0 | ch), static_cast<std::uint8_t>(tr.program)}});
        }
        for (const auto& [sec, start] : passes) {
            const auto& pat = c.patterns[static_cast<std::size_t>(c.sections[static_cast<std::size_t>(sec)].pattern)];
            for (std::uint32_t i = pat.begin; i < pat.end; ++i) {
                const seq::SeqNote& n = c.notes[i];
                if (n.track != t) {
                    continue;
                }
                ev.push_back(Event{start + n.tick, 3, {static_cast<std::uint8_t>(0x90 | ch), n.note, n.velocity}});
                ev.push_back(Event{start + n.off_tick, 2, {static_cast<std::uint8_t>(0x80 | ch), n.note, 0}});
            }
            for (std::uint32_t i = pat.cc_begin; i < pat.cc_end; ++i) { // CC automation lanes
                const seq::SeqCc& cc = c.ccs[i];
                if (cc.track == t) {
                    ev.push_back(Event{start + cc.tick, 1, {static_cast<std::uint8_t>(0xB0 | ch), cc.cc, cc.value}});
                }
            }
        }
        put_track(out, ev, end);
    }
    return out;
}

bool write_smf(const std::filesystem::path& path, const model::Document& doc, const std::string& title,
               std::string* error) {
    const auto bytes = export_smf(doc, title);
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) {
        if (error) {
            *error = "cannot write " + path.string();
        }
        return false;
    }
    return true;
}

std::optional<SmfImport> import_smf(const std::vector<std::uint8_t>& bytes, const model::Song& base,
                                    std::string* error, int bars_per_pattern) {
    const auto fail = [&](const char* what) {
        if (error) {
            *error = what;
        }
        return std::nullopt;
    };
    Reader r{bytes};
    if (r.be(4) != 0x4D546864u) { // "MThd"
        return fail("not a MIDI file");
    }
    const std::uint32_t hlen = r.be(4);
    const int format = static_cast<int>(r.be(2));
    const int ntracks = static_cast<int>(r.be(2));
    const int division = static_cast<int>(r.be(2));
    r.p += hlen > 6 ? hlen - 6 : 0;
    if (!r.ok || format > 1) {
        return fail(format == 2 ? "MIDI file type 2 is not supported" : "bad MIDI header");
    }
    if (division & 0x8000 || division == 0) {
        return fail("SMPTE time division is not supported");
    }
    std::map<std::uint64_t, int> tempo;          // tick -> bpm10
    std::map<std::uint64_t, std::pair<int, int>> sigs; // tick -> num, den
    std::vector<RawTrack> raw;
    for (int t = 0; t < ntracks && r.ok; ++t) {
        if (r.be(4) != 0x4D54726Bu) { // "MTrk"
            return fail("bad MIDI track chunk");
        }
        const std::uint32_t chunk_len = r.be(4);
        const std::size_t end = r.p + chunk_len;
        RawTrack tr;
        std::uint64_t tick = 0;
        std::uint8_t status = 0;
        std::map<int, std::pair<std::uint64_t, std::uint8_t>> open; // ch * 128 + note -> start, vel
        while (r.ok && r.p < end) {
            tick += r.vlq();
            std::uint8_t b = r.u8();
            if (b == 0xFF) {
                const std::uint8_t type = r.u8();
                const auto len = static_cast<std::size_t>(r.vlq());
                const std::size_t at = r.p;
                if (type == 0x51 && len == 3) {
                    const std::uint32_t us = r.be(3);
                    tempo[tick] = us ? static_cast<int>(std::lround(600000000.0 / us)) : 1200;
                } else if (type == 0x58 && len >= 2) {
                    const int num = r.u8();
                    const int den = 1 << std::min<int>(r.u8(), 4);
                    sigs[tick] = {num, den};
                } else if (type == 0x03 && tr.name.empty()) {
                    tr.name.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(std::min(at + len, bytes.size())));
                }
                r.p = at + len;
                continue;
            }
            if (b == 0xF0 || b == 0xF7) { // SysEx: skipped
                r.p += static_cast<std::size_t>(r.vlq());
                continue;
            }
            if (b & 0x80) {
                status = b;
            } else {
                --r.p; // running status
            }
            const int kind = status & 0xF0;
            const int ch = status & 0x0F;
            const std::uint8_t d1 = r.u8();
            const std::uint8_t d2 = kind == 0xC0 || kind == 0xD0 ? 0 : r.u8();
            const int key = ch * 128 + (d1 & 0x7F);
            if (kind == 0xC0 && !tr.program.count(ch)) {
                tr.program[ch] = d1 & 0x7F;
            } else if (kind == 0xB0 && imported_cc(d1)) {
                tr.ccs[ch].push_back(RawCc{tick, d1, static_cast<std::uint8_t>(d2 & 0x7F)});
            } else if (kind == 0x90 && d2 > 0) {
                open[key] = {tick, d2};
            } else if (kind == 0x80 || kind == 0x90) {
                if (const auto it = open.find(key); it != open.end()) {
                    tr.by_channel[ch].push_back(RawNote{it->second.first, std::max<std::uint64_t>(1, tick - it->second.first),
                                                        static_cast<std::uint8_t>(d1 & 0x7F), it->second.second});
                    open.erase(it);
                }
            }
        }
        r.p = end;
        raw.push_back(std::move(tr));
    }
    if (!r.ok) {
        return fail("MIDI file is truncated");
    }

    // Tracks: type 1 one per MTrk with notes, type 0 one per channel.
    struct Lane {
        std::string name;
        std::vector<RawNote> notes;
        int channel = 0;
        int program = -1;
        std::vector<RawCc> ccs;
    };
    std::vector<Lane> lanes;
    for (auto& tr : raw) {
        if (format == 0) {
            for (auto& [ch, notes] : tr.by_channel) {
                const auto prog = tr.program.find(ch);
                lanes.push_back(Lane{"CH " + std::to_string(ch + 1), std::move(notes), ch,
                                     prog == tr.program.end() ? -1 : prog->second, tr.ccs[ch]});
            }
        } else {
            // One lane per MTrk; its channel is the one with the most notes.
            Lane lane{tr.name, {}, 0, -1, {}};
            std::size_t most = 0;
            for (auto& [ch, notes] : tr.by_channel) {
                if (notes.size() > most) {
                    most = notes.size();
                    lane.channel = ch;
                }
                lane.notes.insert(lane.notes.end(), notes.begin(), notes.end());
            }
            const auto prog = tr.program.find(lane.channel);
            lane.program = prog == tr.program.end() ? -1 : prog->second;
            lane.ccs = tr.ccs[lane.channel];
            if (!lane.notes.empty()) {
                lanes.push_back(std::move(lane));
            }
        }
    }
    SmfImport res;
    if (lanes.size() > static_cast<std::size_t>(model::kTracks)) {
        res.warning = "MORE THAN 16 TRACKS: THE REST WERE SKIPPED";
        lanes.resize(model::kTracks);
    }
    const auto to960 = [&](std::uint64_t t) { return static_cast<std::uint64_t>(std::llround(static_cast<double>(t) * kSmfPpq / division)); };
    const auto at_or_before = [](const auto& map, std::uint64_t t, auto fallback) {
        auto it = map.upper_bound(t);
        return it == map.begin() ? fallback : std::prev(it)->second;
    };
    std::map<std::uint64_t, int> tempo960;
    for (const auto& [t, v] : tempo) {
        tempo960[to960(t)] = std::clamp(v, 200, 3000);
    }
    std::map<std::uint64_t, std::pair<int, int>> sig960;
    for (const auto& [t, v] : sigs) {
        sig960[to960(t)] = v;
    }
    std::uint64_t last = 0;
    for (const auto& l : lanes) {
        for (const auto& n : l.notes) {
            last = std::max(last, to960(n.tick + n.length));
        }
    }
    // Pattern length from the first signature: whole bars, at most 256 sixteenths.
    const auto [num0, den0] = at_or_before(sig960, 0, std::pair<int, int>{4, 4});
    const auto bar = static_cast<std::uint64_t>(seq::bar_ticks(std::clamp(num0, 1, 16), den0));
    const std::uint64_t sixteenth = model::kPpq / 4;
    int bars = std::max(1, bars_per_pattern);
    while (bars > 1 && bar * static_cast<std::uint64_t>(bars) / sixteenth > 256) {
        --bars;
    }
    const std::uint64_t chunk = bar * static_cast<std::uint64_t>(bars);
    std::size_t chunks = static_cast<std::size_t>(std::max<std::uint64_t>(1, (last + chunk - 1) / chunk));
    if (chunks > static_cast<std::size_t>(model::kSections)) {
        res.warning = "TRUNCATED AFTER " + std::to_string(model::kSections * bars) + " BARS";
        chunks = model::kSections;
    }

    model::Song& s = res.song;
    s = base;
    s.patterns.assign(chunks, model::Pattern{});
    s.order.clear();
    for (std::size_t k = 0; k < chunks; ++k) {
        model::Pattern& p = s.patterns[k];
        char name[16];
        std::snprintf(name, sizeof name, "MIDI %02zu", k + 1);
        p.name = name;
        p.grid = model::Grid::Sixteenth;
        p.steps = static_cast<int>(std::min<std::uint64_t>(256, chunk / sixteenth));
        const auto [num, den] = at_or_before(sig960, k * chunk, std::pair<int, int>{4, 4});
        s.sections[k] = model::Section{static_cast<int>(k), 1, at_or_before(tempo960, k * chunk, 1200),
                                       std::clamp(num, 1, 16), den == 2 || den == 8 || den == 16 ? den : 4};
        s.order.push_back(static_cast<int>(k));
    }
    for (std::size_t li = 0; li < lanes.size(); ++li) {
        model::Track& tr = s.tracks[li];
        tr.type = model::TrackType::Notes;
        if (!lanes[li].name.empty()) {
            tr.name = lanes[li].name.substr(0, 24);
        }
        for (const auto& n : lanes[li].notes) {
            const std::uint64_t start = to960(n.tick);
            const std::size_t k = static_cast<std::size_t>(start / chunk);
            if (k >= chunks) {
                continue;
            }
            const std::uint64_t in = start - k * chunk;
            const std::uint64_t len = std::min(std::max<std::uint64_t>(1, to960(n.length)), chunk - in);
            model::insert_note(s.patterns[k].lanes[li], model::Note{static_cast<std::uint32_t>(in), static_cast<std::uint32_t>(len),
                                                                    n.note, std::max<std::uint8_t>(1, n.velocity)});
            ++res.notes;
        }
        res.tracks += lanes[li].notes.empty() ? 0 : 1;
        tr.out_channel = lanes[li].channel;
        tr.program = lanes[li].program;
        res.track_info.push_back(SmfTrackInfo{tr.name, lanes[li].program, lanes[li].channel, lanes[li].channel == 9});
        // Controllers -> automation lanes, one value per step (the last one sent in that step).
        for (const auto& cc : lanes[li].ccs) {
            const std::uint64_t at = to960(cc.tick);
            const std::size_t k = static_cast<std::size_t>(at / chunk);
            if (k >= chunks) {
                continue;
            }
            model::Pattern& p = s.patterns[k];
            const auto step = static_cast<std::size_t>((at - k * chunk) / p.step_ticks());
            auto& lane = p.lane_for(static_cast<int>(li), cc.cc);
            if (step < lane.values.size()) {
                lane.values[step] = static_cast<std::int16_t>(cc.value);
                ++res.ccs;
            }
        }
    }
    s.play_mode = model::PlayMode::Song;
    s.current_section = 0;
    res.sections = static_cast<int>(chunks);
    return res;
}

bool is_midi_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext == ".mid" || ext == ".midi";
}

std::optional<SmfImport> read_smf(const std::filesystem::path& path, const model::Song& base, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) {
            *error = "cannot read " + path.string();
        }
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return import_smf(bytes, base, error);
}

} // namespace phos::io
