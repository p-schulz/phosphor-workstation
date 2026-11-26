#include "content/templates.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>

#include "content/presets.h"
#include "engine/offline.h"
#include "io/snes.h"

namespace phos::content {

namespace {

using namespace phos::model;
using presets::chord;
using presets::electric_piano;
using presets::note;
using presets::pad;
using presets::synth_bass;

Pattern two_bars(const char* name) {
    Pattern p;
    p.name = name;
    p.steps = 32;
    p.grid = Grid::Sixteenth;
    return p;
}

void name_track(Song& s, int t, const char* name, int instrument, TrackType type = TrackType::Notes) {
    Track& tr = s.tracks[static_cast<std::size_t>(t)];
    tr.name = name;
    tr.instrument = instrument;
    tr.type = type;
}

// Resamples section `section` (one seamless pass, no master effects) into loop slot 0, played
// on track A (8) and launched by a LOOPS lane (track 4) that spans the whole of `pattern`.
void resample_into_loop(Document& d, int section, const char* name, std::uint32_t rate, LoopSync sync, int pattern) {
    engine::OfflineRender opt;
    opt.section = section;
    opt.seamless = true;
    opt.master = false;
    opt.sample_rate = rate;
    opt.name = name;
    LoopSlot& l = d.song.loops[0];
    l.sample = engine::render_section(d, opt);
    l.name = name;
    l.bars = static_cast<int>(d.song.patterns[static_cast<std::size_t>(d.song.sections[static_cast<std::size_t>(section)].pattern)]
                                  .length_ticks() / (4 * kPpq));
    l.sync = sync;
    l.quantize = LaunchQuantize::Bar;
    l.track = 8;
    d.song.tracks[8].name = "LOOP A";
    Pattern& p = d.song.patterns[static_cast<std::size_t>(pattern)];
    note(p, 4, 0.0, p.length_ticks() / static_cast<double>(kPpq), kLoopBaseNote, 100);
}

// MALLSOFT: Fmaj7 / Em7 electric piano and bass at 96 BPM, played at 72 BPM (x 0.75: five
// semitones down, so the chords become Cmaj7 / Bm7 and the pad follows them).
Document mallsoft(std::uint32_t rate) {
    Document d;
    Song& s = d.song;
    d.instruments[0] = electric_piano();
    d.instruments[1] = pad("PAD");
    d.instruments[2] = synth_bass("BASS", 600.0f, 0.5f);
    d.instruments[2].synth.osc1 = OscWave::Sine;
    d.instruments[3] = make_instrument(InstrumentKind::Drum, "KIT");
    name_track(s, 0, "E.PIANO", 0);
    name_track(s, 1, "PAD", 1);
    name_track(s, 2, "BASS", 2);
    name_track(s, 3, "DRUMS", 3);
    name_track(s, 4, "LOOPS", 0, TrackType::Loops);

    Pattern src = two_bars("SOURCE");
    for (int bar = 0; bar < 2; ++bar) {
        const double b = bar * 4.0;
        const auto ch = bar == 0 ? std::initializer_list<int>{53, 57, 60, 64} : std::initializer_list<int>{52, 55, 59, 62};
        chord(src, 0, b, 1.5, ch, 90);
        chord(src, 0, b + 1.5, 1.0, ch, 72);
        chord(src, 0, b + 3.0, 1.0, ch, 80);
        note(src, 0, b + 2.5, 0.5, bar == 0 ? 69 : 67, 70);
        const int root = bar == 0 ? 41 : 40;
        note(src, 2, b, 1.5, root, 100);
        note(src, 2, b + 2.0, 1.0, root, 90);
        note(src, 2, b + 3.5, 0.5, root + 7, 80);
    }
    Pattern mall = two_bars("MALL");
    for (int bar = 0; bar < 2; ++bar) {
        const double b = bar * 4.0;
        chord(mall, 1, b, 4.0, bar == 0 ? std::initializer_list<int>{48, 55, 59, 64} : std::initializer_list<int>{47, 54, 57, 62}, 64);
        note(mall, 3, b, 0.5, 36, 92);
        note(mall, 3, b + 2.5, 0.5, 36, 70);
        note(mall, 3, b + 1.0, 0.5, 38, 58);
        note(mall, 3, b + 3.0, 0.5, 38, 62);
        for (int e = 0; e < 8; ++e) {
            note(mall, 3, b + e * 0.5, 0.25, 42, e % 2 ? 36 : 48);
        }
    }
    s.patterns = {src, mall};
    s.sections[0] = Section{1, 1, 720, 4, 4};
    s.sections[1] = Section{0, 1, 960, 4, 4};
    s.order = {0};
    resample_into_loop(d, 1, "MALL LOOP", rate, LoopSync::Varispeed, 1);
    s.loops[0].gain_db = -1.0f;
    s.tracks[8].send_a = 0.45f;
    s.tracks[1].send_a = 0.6f;
    s.tracks[1].volume_db = -4.0f;
    s.tracks[3].send_a = 0.12f;
    s.tracks[3].volume_db = -5.0f;
    s.returns[0].p = {0.92f, 0.45f, 35.0f, 1.0f, 1.0f};
    s.return_level[0] = 0.8f;
    s.vapor = 55;
    s.master_db = -2.0f;
    return d;
}

// FUTURE FUNK: Dm9 / G13 stabs and an octave bass at 110 BPM, SLICEd into 16 and played at 118
// (no pitch change); the loop ducks under the kick.
Document future_funk(std::uint32_t rate) {
    Document d;
    Song& s = d.song;
    d.instruments[0] = make_instrument(InstrumentKind::Synth, "STABS");
    Instrument& stabs = d.instruments[0];
    stabs.synth.unison = 2;
    stabs.sampler.amp = Adsr{2.0f, 250.0f, 0.3f, 150.0f};
    stabs.sampler.cutoff_hz = 2500.0f;
    stabs.sampler.resonance = 0.25f;
    stabs.sampler.env_amount = 2.0f;
    stabs.sampler.filter_env = Adsr{1.0f, 220.0f, 0.0f, 150.0f};
    stabs.sampler.volume_db = -6.0f;
    d.instruments[1] = synth_bass("BASS", 700.0f, 2.5f);
    d.instruments[3] = make_instrument(InstrumentKind::Drum, "KIT");
    name_track(s, 0, "STABS", 0);
    name_track(s, 1, "BASS", 1);
    name_track(s, 3, "DRUMS", 3);
    name_track(s, 4, "LOOPS", 0, TrackType::Loops);

    Pattern src = two_bars("SOURCE");
    for (int bar = 0; bar < 2; ++bar) {
        const double b = bar * 4.0;
        const auto ch = bar == 0 ? std::initializer_list<int>{50, 53, 57, 60, 64} : std::initializer_list<int>{55, 59, 64, 65};
        for (const double at : {0.0, 0.75, 1.5, 2.5, 3.25}) {
            chord(src, 0, b + at, 0.4, ch, 100);
        }
        const int root = bar == 0 ? 38 : 43;
        for (int e = 0; e < 8; ++e) {
            note(src, 1, b + e * 0.5, 0.4, root + (e % 2 ? 12 : 0), 100);
        }
    }
    Pattern funk = two_bars("FUNK");
    for (int beat = 0; beat < 8; ++beat) {
        note(funk, 3, beat, 0.5, 36, 112);
        if (beat % 2 == 1) {
            note(funk, 3, beat, 0.5, 39, 96);
        }
        note(funk, 3, beat + 0.25, 0.25, 42, 44);
        note(funk, 3, beat + 0.5, 0.25, 46, 70);
        note(funk, 3, beat + 0.75, 0.25, 42, 52);
    }
    s.patterns = {src, funk};
    s.sections[0] = Section{1, 1, 1180, 4, 4};
    s.sections[1] = Section{0, 1, 1100, 4, 4};
    s.order = {0};
    resample_into_loop(d, 1, "FUNK LOOP", rate, LoopSync::Slice, 1);
    s.loops[0].slices = 16;
    Track& loop = s.tracks[8];
    loop.inserts[0] = make_fx(FxType::Duck);
    loop.inserts[0].p = {3.0f, 0.75f, 150.0f, -28.0f, 0.0f};
    loop.inserts[1] = make_fx(FxType::Chorus);
    loop.inserts[1].p = {0.0f, 0.8f, 9000.0f, 0.0f, 0.35f};
    loop.send_a = 0.12f;
    s.tracks[3].volume_db = -2.0f;
    s.returns[0].p = {0.55f, 0.4f, 10.0f, 1.0f, 1.0f};
    s.vapor = 20;
    s.master_db = -2.0f;
    return d;
}

// CHIP-VAPOR: a GB arpeggio over Am / F with a wave-channel bass at 100 BPM, played at 84.1 BPM
// (x 0.841: three semitones down, F#m / D); a SNES pad and a sparse GB lead on top.
Document chip_vapor(std::uint32_t rate) {
    Document d;
    Song& s = d.song;
    d.instruments[0] = make_instrument(InstrumentKind::Gb, "GB LEAD");
    d.instruments[1] = make_instrument(InstrumentKind::Gb, "GB BASS");
    d.instruments[1].gb = default_gb(GbChannel::Wave);
    d.instruments[2] = presets::snes_pad(rate);
    d.instruments[3] = make_instrument(InstrumentKind::Drum, "KIT");
    name_track(s, 0, "GB LEAD", 0);
    name_track(s, 1, "GB BASS", 1);
    name_track(s, 2, "SNES PAD", 2);
    name_track(s, 3, "DRUMS", 3);
    name_track(s, 4, "LOOPS", 0, TrackType::Loops);

    Pattern src = two_bars("SOURCE");
    for (int bar = 0; bar < 2; ++bar) {
        const double b = bar * 4.0;
        const std::array<int, 4> arp = bar == 0 ? std::array<int, 4>{69, 72, 76, 81} : std::array<int, 4>{65, 69, 72, 77};
        for (int k = 0; k < 16; ++k) {
            note(src, 0, b + k * 0.25, 0.2, arp[static_cast<std::size_t>(k % 4)], 100);
        }
        const int root = bar == 0 ? 45 : 41;
        for (int e = 0; e < 8; ++e) {
            note(src, 1, b + e * 0.5, 0.4, root + (e % 2 ? 12 : 0), 100);
        }
        note(src, 3, b, 0.5, 36, 110);
        note(src, 3, b + 2.0, 0.5, 36, 100);
        note(src, 3, b + 1.0, 0.5, 38, 90);
        note(src, 3, b + 3.0, 0.5, 38, 95);
        for (int e = 0; e < 8; ++e) {
            note(src, 3, b + e * 0.5, 0.25, 42, 50);
        }
    }
    Pattern main = two_bars("CHIP VAPOR");
    chord(main, 2, 0.0, 4.0, {54, 57, 61}, 90);
    chord(main, 2, 4.0, 4.0, {50, 54, 57}, 90);
    note(main, 0, 0.0, 1.5, 78, 100);
    note(main, 0, 2.0, 1.0, 76, 100);
    note(main, 0, 3.0, 1.0, 73, 100);
    note(main, 0, 4.0, 2.0, 74, 100);
    note(main, 0, 6.5, 1.5, 69, 100);
    s.patterns = {src, main};
    s.sections[0] = Section{1, 1, 841, 4, 4};
    s.sections[1] = Section{0, 1, 1000, 4, 4};
    s.order = {0};
    resample_into_loop(d, 1, "CHIP LOOP", rate, LoopSync::Varispeed, 1);
    s.loops[0].gain_db = -3.0f;
    s.tracks[0].volume_db = -9.0f;
    s.tracks[0].send_a = 0.35f;
    s.tracks[8].send_a = 0.3f;
    s.returns[0].p = {0.8f, 0.5f, 25.0f, 1.0f, 1.0f};
    s.vapor = 40;
    s.master_db = -2.0f;
    return d;
}

std::string normalise(std::string_view name) {
    std::string out;
    for (const char c : name) {
        out += c == ' ' || c == '_' ? '-' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

} // namespace

std::vector<std::string_view> template_names() { return {"MALLSOFT", "FUTURE-FUNK", "CHIP-VAPOR"}; }

bool is_template(std::string_view name) {
    const std::string n = normalise(name);
    return n == "MALLSOFT" || n == "FUTURE-FUNK" || n == "FUTUREFUNK" || n == "CHIP-VAPOR" || n == "CHIPVAPOR";
}

std::optional<model::Document> make_template(std::string_view name, std::uint32_t sample_rate) {
    const std::string n = normalise(name);
    if (n == "MALLSOFT") {
        return mallsoft(sample_rate);
    }
    if (n == "FUTURE-FUNK" || n == "FUTUREFUNK") {
        return future_funk(sample_rate);
    }
    if (n == "CHIP-VAPOR" || n == "CHIPVAPOR") {
        return chip_vapor(sample_rate);
    }
    return std::nullopt;
}

} // namespace phos::content
