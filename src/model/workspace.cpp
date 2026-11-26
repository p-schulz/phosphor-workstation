#include "model/workspace.h"

#include <algorithm>

namespace phos::model {

void Workspace::set_instrument(int index, Instrument after, std::string label, double now_s) {
    index = std::clamp(index, 0, kInstruments - 1);
    Instrument& slot = doc_.instruments[static_cast<std::size_t>(index)];
    if (slot == after) {
        return;
    }
    const bool merge = !undo_.empty() && undo_.back().index == index && undo_.back().label == label &&
                       now_s - undo_.back().time_s < 1.0 && redo_.empty();
    if (merge) {
        undo_.back().after = after;
        undo_.back().time_s = now_s;
    } else {
        Step s;
        s.label = std::move(label);
        s.index = index;
        s.before = slot;
        s.after = after;
        s.time_s = now_s;
        push(std::move(s));
    }
    slot = std::move(after);
    ++revision_;
}

void Workspace::select_track(int track) {
    track = std::clamp(track, 0, kTracks - 1);
    doc_.song.current_track = track;
    doc_.current = doc_.song.tracks[static_cast<std::size_t>(track)].instrument;
    ++revision_;
}

void Workspace::select_section(int section) {
    doc_.song.current_section = std::clamp(section, 0, kSections - 1);
    ++revision_;
}

void Workspace::select(int index) {
    index = std::clamp(index, 0, kInstruments - 1);
    if (doc_.current != index) {
        doc_.current = index;
        ++revision_;
    }
}

void Workspace::push(Step s) {
    redo_.clear();
    undo_.push_back(std::move(s));
    if (undo_.size() > kUndoSteps) {
        undo_.pop_front();
    }
}

void Workspace::apply(const Step& s, bool forward) {
    if (s.index == kWholeDocument) {
        const int current = doc_.current;
        doc_ = forward ? *s.doc_after : *s.doc_before;
        doc_.current = current;
    } else if (s.index < 0) {
        doc_.song = forward ? *s.song_after : *s.song_before;
    } else {
        doc_.instruments[static_cast<std::size_t>(s.index)] = forward ? s.after : s.before;
        doc_.current = s.index;
    }
    ++revision_;
}

void Workspace::set_document(Document after, std::string label) {
    Step s;
    s.label = std::move(label);
    s.index = kWholeDocument;
    s.doc_before = doc_;
    s.doc_after = after;
    const int current = doc_.current;
    doc_ = std::move(after);
    doc_.current = current;
    push(std::move(s));
    ++revision_;
}

void Workspace::set_song(Song after, std::string label, double now_s) {
    if (doc_.song == after) {
        return;
    }
    const bool merge = !undo_.empty() && undo_.back().index == -1 && undo_.back().label == label &&
                       now_s - undo_.back().time_s < 1.0 && redo_.empty();
    if (merge) {
        undo_.back().song_after = after;
        undo_.back().time_s = now_s;
    } else {
        Step s;
        s.label = std::move(label);
        s.song_before = doc_.song;
        s.song_after = after;
        s.time_s = now_s;
        push(std::move(s));
    }
    doc_.song = std::move(after);
    ++revision_;
}

void Workspace::begin_take() { take_before_ = doc_.song; }

void Workspace::commit_take(std::string label) {
    if (!take_before_) {
        return;
    }
    if (!(*take_before_ == doc_.song)) {
        Step s;
        s.label = std::move(label);
        s.song_before = std::move(*take_before_);
        s.song_after = doc_.song;
        push(std::move(s));
    }
    take_before_.reset();
}

void Workspace::replace(Document doc) {
    doc_ = std::move(doc);
    undo_.clear();
    redo_.clear();
    take_before_.reset();
    ++revision_;
    saved_revision_ = revision_;
}

std::optional<std::string> Workspace::undo() {
    if (undo_.empty()) {
        return std::nullopt;
    }
    Step s = std::move(undo_.back());
    undo_.pop_back();
    apply(s, false);
    std::string label = s.label;
    redo_.push_back(std::move(s));
    return label;
}

std::optional<std::string> Workspace::redo() {
    if (redo_.empty()) {
        return std::nullopt;
    }
    Step s = std::move(redo_.back());
    redo_.pop_back();
    apply(s, true);
    std::string label = s.label;
    s.time_s = -10.0; // a redone step never merges with the next edit
    undo_.push_back(std::move(s));
    return label;
}

} // namespace phos::model
