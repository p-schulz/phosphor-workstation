#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "model/song.h"

// The song as the audio thread sees it (PLAN.md §6.3: flat, sorted event arrays). Built on the
// UI thread by compile(), carried in the engine snapshot, never modified afterwards.
namespace phos::seq {

struct SeqNote {
    std::uint32_t tick = 0;     // swing applied
    std::uint32_t off_tick = 0; // tick + length, swing applied
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
    std::uint8_t track = 0;
};

// A CC automation value at its step (swing applied).
struct SeqCc {
    std::uint32_t tick = 0;
    std::uint8_t cc = 0;
    std::uint8_t value = 0;
    std::uint8_t track = 0;
};

struct SeqPattern {
    std::uint32_t length_ticks = 960 * 4;
    std::uint32_t begin = 0; // [begin, end) into CompiledSong::notes, sorted by tick
    std::uint32_t end = 0;
    std::uint32_t cc_begin = 0; // [cc_begin, cc_end) into CompiledSong::ccs, sorted by tick
    std::uint32_t cc_end = 0;
};

struct SeqSection {
    int pattern = 0;
    int repeats = 1;
    int bpm10 = 1200;
    int sig_num = 4;
    int sig_den = 4;
};

struct TrackBlock {
    int instrument = 0;
    model::MidiDest dest = model::MidiDest::Internal; // M8
    int out_channel = 0;
    int program = -1;
    float gain = 1.0f; // volume, mute and solo folded in
    float pan = 0.0f;
    model::TrackType type = model::TrackType::Notes;
    std::array<model::FxSlot, 2> inserts{};
    float send_a = 0.0f; // post-fader
    float send_b = 0.0f;
};

struct CompiledSong {
    std::vector<SeqNote> notes;
    std::vector<SeqCc> ccs;
    std::vector<SeqPattern> patterns;
    std::array<SeqSection, model::kSections> sections{};
    std::vector<int> order{0};
    model::PlayMode play_mode = model::PlayMode::Pattern;
    int current_section = 0;
    model::MetronomeMode metronome = model::MetronomeMode::Record;
    int count_in_bars = 1;
    int record_offset_ms = 0;
    std::array<TrackBlock, model::kTracks> tracks{};
    int current_track = 0;
    model::SnesEcho snes_echo;
    bool strict_cmi = false;
    bool strict_gb = false;
    bool strict_snes = false;
    std::array<model::FxSlot, 2> returns{};
    std::array<float, 2> return_level{1.0f, 1.0f};
    std::array<model::FxSlot, 2> master_fx{};
    int vapor = 0; // 0..100

    CompiledSong();
};

CompiledSong compile(const model::Song& song);

} // namespace phos::seq
