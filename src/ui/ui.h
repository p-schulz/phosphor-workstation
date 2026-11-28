#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/font.h"
#include "gfx/palette.h"
#include "ui/app_context.h"
#include "ui/command_line.h"
#include "ui/input.h"
#include "ui/key_map.h"
#include "ui/page.h"
#include "ui/side_keys.h"

namespace phos::ui {

struct Fonts {
    gfx::BitmapFont large; // 8x12, the main cell font
    gfx::BitmapFont small; // 8x8, labels
};

enum class Focus { Command, Fields };

enum class ZoneOwner { Chrome, Page };

// Root of the user interface: page router, command line, focus, light-pen zones, INFO messages.
// Contains no GL and no device code; the windowed app and the headless renderer drive it alike.
class Ui {
public:
    Ui(AppContext& app, const Fonts& fonts, KeyMap keys);
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    void update(const InputState& input);
    // Renders chrome + current page into `canvas` (light pen not included).
    void draw(gfx::Canvas& canvas);
    // Light-pen crosshair overlay; drawn onto the copy that goes to the screen, whose canvas part
    // starts at x = origin_x (the width of a side strip).
    void draw_pen(gfx::Canvas& canvas, int origin_x = 0) const;

    // Side keys (side_keys.h): the strip width in canvas pixels the host laid out (0 = none), the
    // strips drawn onto the screen copy, and what a key does.
    void set_side_width(int w) { side_.set_width(w); }
    const SideKeys& side_keys() const { return side_; }
    void draw_side_keys(gfx::Canvas& screen) const { side_.draw(screen, *this); }
    void side_key(SideKey key);

    // Runs a command line as if typed. Returns false if it produced an error message.
    bool execute(std::string_view text);
    bool go_to_page(std::string_view id);
    void go_back();

    void message(std::string text, bool error = false);
    const std::string& message_text() const { return message_; }
    bool message_is_error() const { return message_error_; }
    // AppContext::time_s() when the message was set (the INFO line rolls long ones from there).
    double message_time() const { return message_time_; }

    AppContext& app() { return app_; }
    const AppContext& app() const { return app_; }
    const Fonts& fonts() const { return fonts_; }
    const gfx::Theme& theme() const;
    const KeyMap& keys() const { return keys_; }
    const CommandLine& command_line() const { return command_; }
    Focus focus() const { return focus_; }
    void set_focus(Focus focus);

    // KEYJAZZ: while on, note keys play notes and the command line takes no typing (ESC = off).
    bool keyjazz() const { return keyjazz_; }
    void set_keyjazz(bool on);
    void keyjazz_octave(int delta);

    std::string_view current_page_id() const;
    std::string_view previous_page_id() const { return previous_; }
    const Page* current_page() const;
    // Shows a view of the current page ("OUT" on Page 3). False if the page has no such view.
    bool show_view(std::string_view view);

    // Light-pen zones, registered while drawing. Hit-tested against the next frame's pen.
    void add_zone(gfx::Rect rect, ZoneOwner owner, int id) const;
    bool hovered(ZoneOwner owner, int id) const;
    bool pen_inside() const { return pen_inside_; }

private:
    struct Zone {
        gfx::Rect rect;
        ZoneOwner owner;
        int id;
    };

    Page* find_page(std::string_view id) const;
    Page* page();
    void handle_event(const InputEvent& event);
    void handle_command_key(const InputEvent& event);
    void handle_chrome_zone(int id);
    void handle_pen(const InputState& input);
    void value_step(int dir);
    void step_page(int dir);
    std::optional<std::string> run_ui_command(const Command& cmd);
    bool handle_keyjazz(const InputEvent& event);
    void release_keyjazz_notes();

    AppContext& app_;
    Fonts fonts_;
    KeyMap keys_;
    std::vector<std::unique_ptr<Page>> pages_;
    std::string current_ = "1";
    std::string previous_ = "1";
    CommandLine command_;
    Focus focus_ = Focus::Command;
    std::string message_;
    bool message_error_ = false;
    double message_time_ = 0.0;
    bool suppress_text_ = false;
    bool keyjazz_ = false;
    std::array<int, 128> keyjazz_down_{}; // per key label: sounding note + 1 (0 = up)
    std::vector<double> taps_;            // TAP tempo history

    mutable std::vector<Zone> zones_;
    std::vector<Zone> hit_zones_; // zones from the last completed draw
    bool pen_inside_ = false;
    SideKeys side_;
    float pen_x_ = 0.0f;
    float pen_y_ = 0.0f;
    int hover_index_ = -1;
};

} // namespace phos::ui
