#include "ui/command_line.h"

#include "ui/commands.h"

namespace phos::ui {

CommandLine::CommandLine(std::size_t max_length, std::size_t history_limit)
    : max_length_(max_length), history_limit_(history_limit) {}

void CommandLine::insert(char c) {
    if (text_.size() >= max_length_ || static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
        return;
    }
    text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(cursor_), c);
    ++cursor_;
    history_pos_ = -1;
}

void CommandLine::backspace() {
    if (cursor_ == 0) {
        return;
    }
    text_.erase(cursor_ - 1, 1);
    --cursor_;
    history_pos_ = -1;
}

void CommandLine::erase() {
    if (cursor_ < text_.size()) {
        text_.erase(cursor_, 1);
        history_pos_ = -1;
    }
}

void CommandLine::left() {
    if (cursor_ > 0) {
        --cursor_;
    }
}

void CommandLine::right() {
    if (cursor_ < text_.size()) {
        ++cursor_;
    }
}

void CommandLine::home() { cursor_ = 0; }

void CommandLine::end() { cursor_ = text_.size(); }

void CommandLine::clear() {
    text_.clear();
    cursor_ = 0;
    history_pos_ = -1;
}

void CommandLine::set_text(std::string text) {
    if (text.size() > max_length_) {
        text.resize(max_length_);
    }
    text_ = std::move(text);
    cursor_ = text_.size();
}

void CommandLine::history_prev() {
    if (history_.empty()) {
        return;
    }
    if (history_pos_ < 0) {
        draft_ = text_;
        history_pos_ = static_cast<int>(history_.size()) - 1;
    } else if (history_pos_ > 0) {
        --history_pos_;
    }
    set_text(history_[static_cast<std::size_t>(history_pos_)]);
}

void CommandLine::history_next() {
    if (history_pos_ < 0) {
        return;
    }
    if (history_pos_ + 1 < static_cast<int>(history_.size())) {
        ++history_pos_;
        set_text(history_[static_cast<std::size_t>(history_pos_)]);
    } else {
        history_pos_ = -1;
        set_text(draft_);
    }
}

void CommandLine::complete() {
    if (cursor_ != text_.size()) {
        return;
    }
    set_text(complete_command(text_));
    history_pos_ = -1;
}

std::string CommandLine::submit() {
    std::string out = text_;
    if (!out.empty() && (history_.empty() || history_.back() != out)) {
        history_.push_back(out);
        if (history_.size() > history_limit_) {
            history_.erase(history_.begin());
        }
    }
    clear();
    return out;
}

} // namespace phos::ui
