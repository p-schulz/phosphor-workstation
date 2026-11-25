#include <catch2/catch_test_macros.hpp>

#include <string>

#include "gfx/canvas.h"
#include "ui/layout.h"
#include "ui/render.h"
#include "ui/ui.h"

using namespace phos;
using namespace phos::ui;

namespace {

const Fonts& fonts() {
    static const Fonts f = [] {
        std::string error;
        auto loaded = load_fonts(PHOS_ASSET_DIR, &error);
        REQUIRE(loaded);
        return *loaded;
    }();
    return f;
}

InputEvent key(Key k, int mods = 0) {
    InputEvent e;
    e.type = InputEvent::Type::Key;
    e.key = k;
    e.mods = mods;
    return e;
}

void type(InputState& in, std::string_view text) {
    for (char c : text) {
        InputEvent k;
        k.type = InputEvent::Type::Key;
        k.key = c == ' ' ? Key::Space : Key::Char;
        k.ch = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        in.events.push_back(k);
        InputEvent t;
        t.type = InputEvent::Type::Text;
        t.ch = c;
        in.events.push_back(t);
    }
}

struct Fixture {
    HeadlessContext ctx;
    Ui ui{ctx, fonts(), KeyMap::defaults(false)};
    gfx::Canvas canvas{kCanvasW, kCanvasH};

    void step(InputState& in) {
        ui.draw(canvas); // registers pen zones
        ui.update(in);
        ui.draw(canvas);
        in.begin_frame();
    }
};

} // namespace

TEST_CASE("typing a page command switches pages", "[ui]") {
    Fixture f;
    InputState in;
    type(in, "ps");
    in.events.push_back(key(Key::Enter));
    f.step(in);
    CHECK(f.ui.current_page_id() == "S");
    CHECK(f.ui.command_line().empty());
}

TEST_CASE("space in a command is typed, not bound", "[ui]") {
    Fixture f;
    InputState in;
    type(in, "theme amber");
    f.step(in);
    CHECK(f.ui.command_line().text() == "theme amber");
    in.events.push_back(key(Key::Enter));
    f.step(in);
    CHECK(f.ctx.profile().theme == "AMBER");
    CHECK(f.ui.theme().name == "AMBER");
}

TEST_CASE("function keys follow the key map and unbuilt pages report their milestone", "[ui]") {
    Fixture f;
    InputState in;
    in.events.push_back(key(Key::F12)); // PR, arrives in M4
    f.step(in);
    CHECK(f.ui.current_page_id() == "1");
    CHECK(f.ui.message_is_error());
    CHECK(f.ui.message_text().find("M4") != std::string::npos);
}

TEST_CASE("unknown and future commands give INFO errors", "[ui]") {
    Fixture f;
    CHECK_FALSE(f.ui.execute("FROBNICATE"));
    CHECK(f.ui.message_text() == "UNKNOWN COMMAND: FROBNICATE");
    CHECK_FALSE(f.ui.execute("tempo 90"));
    CHECK(f.ui.message_text().find("M4") != std::string::npos);
}

TEST_CASE("setup page fields change the display profile", "[ui]") {
    Fixture f;
    REQUIRE(f.ui.execute("PS"));
    InputState in;
    in.events.push_back(key(Key::Down)); // empty command line: enter fields (FULLSCREEN)
    in.events.push_back(key(Key::Down)); // AUTO APPLY
    in.events.push_back(key(Key::Down)); // SCALE
    in.events.push_back(key(Key::Right));
    f.step(in);
    CHECK(f.ui.focus() == Focus::Fields);
    CHECK(f.ctx.profile().scale == gfx::ScaleMode::Integer);

    in.events.push_back(key(Key::Escape));
    f.step(in);
    CHECK(f.ui.focus() == Focus::Command);
}

TEST_CASE("light pen selects a page on the index", "[ui]") {
    Fixture f;
    InputState in;
    // INDEX list starts at row 4, col 26; row 4 + 13 = PAGE S.
    in.pen_inside = true;
    in.pen_x = static_cast<float>(px(28));
    in.pen_y = static_cast<float>(py(4 + 13) + 4);
    in.pen_pressed = true;
    in.pen_down = true;
    f.step(in);
    CHECK(f.ui.current_page_id() == "S");
}

TEST_CASE("testcard closes on any key and returns", "[ui]") {
    Fixture f;
    REQUIRE(f.ui.execute("PS"));
    REQUIRE(f.ui.execute("TESTCARD"));
    CHECK(f.ui.current_page_id() == "TESTCARD");
    InputState in;
    in.events.push_back(key(Key::Escape));
    f.step(in);
    CHECK(f.ui.current_page_id() == "S");
}

TEST_CASE("headless render covers every implemented page and rejects the rest", "[ui]") {
    std::string error;
    for (const char* page : {"1", "S", "H", "TESTCARD"}) {
        INFO(page);
        RenderRequest req;
        req.page = page;
        CHECK(render_page(req, fonts(), &error));
    }
    RenderRequest r;
    r.page = "R";
    CHECK_FALSE(render_page(r, fonts(), &error));
    CHECK(error.find("M4") != std::string::npos);
}
