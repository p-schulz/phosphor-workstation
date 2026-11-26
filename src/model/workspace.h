#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include "model/instrument.h"
#include "model/song.h"

namespace phos::model {

inline constexpr int kInstruments = 16;

// The document the UI edits (PLAN.md §6.3): instrument bank and song.
struct Document {
    std::array<Instrument, kInstruments> instruments{};
    int current = 0; // instrument played by MIDI (OMNI) and shown on Pages 7 and 8
    Song song;

    Instrument& current_instrument() { return instruments[static_cast<std::size_t>(current)]; }
    const Instrument& current_instrument() const { return instruments[static_cast<std::size_t>(current)]; }
};

// Document + undo/redo (PLAN.md §7.9: command pattern, 200 steps). Each undoable command records
// the instrument (or the song) before and after; sample PCM is shared between steps, so a
// parameter change costs no copy of the audio. Consecutive changes of the same field within a
// second merge into one step.
class Workspace {
public:
    static constexpr std::size_t kUndoSteps = 200;

    const Document& doc() const { return doc_; }
    // Bumped on every change: the app republishes the engine snapshot when it moves.
    std::uint64_t revision() const { return revision_; }

    // Replaces instrument `index` as one undoable step called `label`. `now_s` merges repeats.
    void set_instrument(int index, Instrument after, std::string label, double now_s = -10.0);
    void select(int index);
    // Navigation (no undo step): the track also selects its instrument for Pages 7/8.
    void select_track(int track);
    void select_section(int section);
    // Replaces instruments and song together as one undoable step (MIDI import with instruments).
    void set_document(Document after, std::string label);
    // Replaces the song as one undoable step.
    void set_song(Song after, std::string label, double now_s = -10.0);
    // Recording: notes go into the song as they arrive (visible, audible on the next pass) and the
    // whole take becomes one step on commit. abort_take() discards nothing, it just ends the take.
    void begin_take();
    Song& take_song() { ++revision_; return doc_.song; }
    void commit_take(std::string label);
    bool in_take() const { return take_before_.has_value(); }
    // New / loaded document: replaces everything and clears undo.
    void replace(Document doc);
    // Marks the document as saved (for the "modified" flag and autosave).
    void mark_saved() { saved_revision_ = revision_; }
    bool modified() const { return revision_ != saved_revision_; }

    std::optional<std::string> undo(); // label of the undone step
    std::optional<std::string> redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    std::size_t undo_depth() const { return undo_.size(); }

private:
    struct Step {
        std::string label;
        int index = -1; // instrument index, -1 = song step
        Instrument before;
        Instrument after;
        std::optional<Song> song_before;
        std::optional<Song> song_after;
        std::optional<Document> doc_before; // index == kWholeDocument
        std::optional<Document> doc_after;
        double time_s = 0.0;
    };
    static constexpr int kWholeDocument = -2;
    void push(Step s);
    void apply(const Step& s, bool forward);

    Document doc_;
    std::optional<Song> take_before_;
    std::uint64_t saved_revision_ = 1;
    std::deque<Step> undo_;
    std::deque<Step> redo_;
    std::uint64_t revision_ = 1;
};

} // namespace phos::model
