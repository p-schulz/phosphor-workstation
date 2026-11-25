#pragma once

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
    // Light-pen crosshair overlay; drawn onto the copy that goes to the screen.
    void draw_pen(gfx::Canvas& canvas) const;

    // Runs a command line as if typed. Returns false if it produced an error message.
    bool execute(std::string_view text);
    bool go_to_page(std::string_view id);
    void go_back();

    void message(std::string text, bool error = false);
    const std::string& message_text() const { return message_; }
    bool message_is_error() const { return message_error_; }

    AppContext& app() { return app_; }
    const AppContext& app() const { return app_; }
    const Fonts& fonts() const { return fonts_; }
    const gfx::Theme& theme() const;
    const KeyMap& keys() const { return keys_; }
    const CommandLine& command_line() const { return command_; }
    Focus focus() const { return focus_; }
    void set_focus(Focus focus);

    std::string_view current_page_id() const;
    std::string_view previous_page_id() const { return previous_; }
    const Page* current_page() const;

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
    std::optional<std::string> run_ui_command(const Command& cmd);

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
    bool suppress_text_ = false;

    mutable std::vector<Zone> zones_;
    std::vector<Zone> hit_zones_; // zones from the last completed draw
    bool pen_inside_ = false;
    float pen_x_ = 0.0f;
    float pen_y_ = 0.0f;
    int hover_index_ = -1;
};

} // namespace phos::ui
