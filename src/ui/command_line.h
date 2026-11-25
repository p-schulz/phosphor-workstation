#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace phos::ui {

// Single-line editor with history (↑/↓) and Tab completion.
class CommandLine {
public:
    explicit CommandLine(std::size_t max_length = 43, std::size_t history_limit = 64);

    const std::string& text() const { return text_; }
    std::size_t cursor() const { return cursor_; }
    bool empty() const { return text_.empty(); }
    // True while ↑/↓ browse history (so ↓ keeps walking history instead of leaving the line).
    bool browsing_history() const { return history_pos_ >= 0; }

    void insert(char c);
    void backspace();
    void erase();
    void left();
    void right();
    void home();
    void end();
    void clear();
    void set_text(std::string text);

    void history_prev();
    void history_next();
    const std::vector<std::string>& history() const { return history_; }

    void complete();

    // Returns the current text, appends it to history (if non-empty and not a repeat) and clears.
    std::string submit();

private:
    std::size_t max_length_;
    std::size_t history_limit_;
    std::string text_;
    std::size_t cursor_ = 0;
    std::vector<std::string> history_;
    int history_pos_ = -1; // -1 = editing a new line
    std::string draft_;
};

} // namespace phos::ui
