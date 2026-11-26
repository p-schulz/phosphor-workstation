#include <catch2/catch_test_macros.hpp>

#include <string>

#include "gfx/canvas.h"
#include "ui/layout.h"
#include "ui/chrome.h"
#include "ui/page_catalog.h"
#include "ui/pages/automation_view.h"
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

TEST_CASE("function keys follow the key map; every catalogued page is built", "[ui]") {
    Fixture f;
    InputState in;
    in.events.push_back(key(Key::F12)); // PR
    f.step(in);
    CHECK(f.ui.current_page_id() == "R");
    in.events.push_back(key(Key::F10)); // PL (M7)
    f.step(in);
    CHECK(f.ui.current_page_id() == "L");
    for (const auto& p : kPageCatalog) {
        INFO(p.id);
        CHECK(p.milestone.empty()); // M7 built the last one; a future page reports its milestone
    }
    CHECK_FALSE(f.ui.execute("PX"));
    CHECK(f.ui.message_is_error());
}

TEST_CASE("unknown and future commands give INFO errors", "[ui]") {
    Fixture f;
    CHECK_FALSE(f.ui.execute("FROBNICATE"));
    CHECK(f.ui.message_text() == "UNKNOWN COMMAND: FROBNICATE");
    for (const auto& v : verb_table()) {
        INFO(std::string(v.verb));
        CHECK(v.milestone.empty()); // M8 built the last planned verb (RENDER)
    }
    CHECK_FALSE(f.ui.execute("render stems")); // headless: no engine to render with
    CHECK(f.ui.message_text() == "ERROR: RENDER NEEDS THE AUDIO ENGINE");
    CHECK(f.ui.execute("vapor 50")); // M7
    CHECK(f.ctx.workspace().doc().song.vapor == 50);
    CHECK(f.ui.execute("tempo 90.5"));
    CHECK(f.ctx.workspace().doc().song.sections[0].bpm10 == 905);
    CHECK_FALSE(f.ui.execute("tempo 400"));
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
    // INDEX list starts at row 4, col 26, one catalog entry per row.
    int s_index = -1;
    for (std::size_t i = 0; i < kPageCatalog.size(); ++i) {
        if (kPageCatalog[i].id == "S") {
            s_index = static_cast<int>(i);
        }
    }
    REQUIRE(s_index >= 0);
    in.pen_inside = true;
    in.pen_x = static_cast<float>(px(28));
    in.pen_y = static_cast<float>(py(4 + s_index) + 4);
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
    for (const char* page : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "C", "D", "K", "L", "R", "M", "S", "H", "TESTCARD"}) {
        INFO(page);
        RenderRequest req;
        req.page = page;
        CHECK(render_page(req, fonts(), &error));
    }
    RenderRequest r;
    r.page = "X";
    CHECK_FALSE(render_page(r, fonts(), &error));
}

TEST_CASE("KEYJAZZ plays notes on key down/up and keeps the command line quiet", "[ui][keyjazz]") {
    Fixture f;
    REQUIRE(f.ui.execute("KEYJAZZ ON"));
    CHECK(f.ui.keyjazz());
    InputState in;
    auto press = [&](char ch, bool release = false, bool repeat = false) {
        InputEvent e = key(Key::Char);
        e.ch = ch;
        e.release = release;
        e.repeat = repeat;
        in.events.push_back(e);
        if (!release) {
            InputEvent t;
            t.type = InputEvent::Type::Text;
            t.ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
            in.events.push_back(t);
        }
    };
    press('Z');
    press('Z', false, true); // key repeat: no second note
    press('Q');
    f.step(in);
    CHECK(f.ctx.keyjazz_held() == std::vector<int>{60, 72});
    CHECK(f.ui.command_line().text().empty());

    press(']'); // octave up while Z is held: Z still releases the note it started
    press('Z', true);
    f.step(in);
    CHECK(f.ctx.keyjazz_held() == std::vector<int>{72});
    CHECK(f.ctx.midi_config().keyjazz_octave == 5);

    press('Z');
    f.step(in);
    CHECK(f.ctx.keyjazz_held() == std::vector<int>{72}); // Z now plays C5 too (Q still holds it)
    in.events.push_back(key(Key::Escape)); // KEYJAZZ off releases everything
    f.step(in);
    CHECK_FALSE(f.ui.keyjazz());
    CHECK(f.ctx.keyjazz_held().empty());
}

#include <chrono>

#include "engine/wavetables.h"
#include "model/wavetable.h"

TEST_CASE("wavetable drawing stays within a 60 fps frame", "[ui][perf]") {
    Fixture f;
    model::Instrument inst = model::make_instrument(model::InstrumentKind::Wavetable, "BIG");
    inst.wavetable = std::make_shared<const model::WavetableData>(model::wavetable::resize(*inst.wavetable, 128));
    f.ctx.workspace().set_instrument(0, inst, "SETUP");
    struct Case {
        const char* page;
        float x0, x1, y; // pen sweep in canvas pixels
    };
    for (const Case cs : {Case{"6", 20.0f, 490.0f, 150.0f}, Case{"0", 12.0f, 236.0f, 120.0f}, Case{"D", 0.0f, 0.0f, 0.0f},
                          Case{"4", 20.0f, 490.0f, 100.0f}}) {
        REQUIRE(f.ui.execute(std::string("P") + cs.page));
        InputState in;
        model::WavetableRef built_from;
        std::shared_ptr<const engine::WaveTables> tables;
        double total_ms = 0.0;
        double worst_ms = 0.0;
        constexpr int kFrames = 120;
        for (int frame = 0; frame < kFrames; ++frame) {
            const auto t0 = std::chrono::steady_clock::now();
            in.pen_inside = cs.x1 > 0.0f;
            in.pen_down = in.pen_inside;
            in.pen_pressed = frame == 0 && in.pen_inside;
            in.pen_x = cs.x0 + (cs.x1 - cs.x0) * static_cast<float>(frame) / kFrames;
            in.pen_y = cs.y + 30.0f * std::sin(static_cast<float>(frame) * 0.2f);
            f.ui.update(in);
            f.ui.draw(f.canvas);
            // What the app does after an edit: rebuild the band-limited tables for the engine.
            const auto& now = f.ctx.workspace().doc().current_instrument();
            if (now.wavetable && now.wavetable != built_from) {
                tables = engine::build_tables(*now.wavetable, built_from.get(), tables.get());
                built_from = now.wavetable;
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            total_ms += ms;
            worst_ms = std::max(worst_ms, ms);
            in.begin_frame();
        }
        const double avg = total_ms / kFrames;
        WARN("page " << cs.page << ": avg " << avg << " ms, worst " << worst_ms << " ms per frame (128 segments)");
#if defined(NDEBUG)
        CHECK(avg < 8.0); // half of a 60 fps frame
        CHECK(worst_ms < 16.6);
#endif
    }
    CHECK(f.ctx.workspace().undo_depth() <= 6); // each sweep merged into one step (+ SETUP)
}

TEST_CASE("M7 verbs edit loops, effects, sends and VAPOR (undoable)", "[ui][vapor]") {
    Fixture f;
    auto& ws = f.ctx.workspace();
    model::Document doc = ws.doc();
    doc.instruments[2].name = "BREAK";
    doc.instruments[2].sample = model::make_sample("BREAK", "", 48000, std::vector<float>(48000 * 4));
    ws.replace(doc);
    const auto& s = [&]() -> const model::Song& { return ws.doc().song; };

    CHECK(f.ui.execute("LOOP 2 FROM 3"));
    CHECK(s().loops[1].sample == ws.doc().instruments[2].sample);
    CHECK(s().loops[1].bars == 2); // 4 s at 120 BPM
    CHECK(f.ui.execute("LOOP 2 SYNC STRETCH"));
    CHECK(s().loops[1].sync == model::LoopSync::Stretch);
    CHECK(f.ui.execute("loop l2 sync slice"));
    CHECK(s().loops[1].sync == model::LoopSync::Slice);
    CHECK(f.ui.execute("LOOP 2 QUANT BEAT"));
    CHECK(s().loops[1].quantize == model::LaunchQuantize::Beat);
    CHECK(f.ui.execute("LOOP 2 BARS 4"));
    CHECK(s().loops[1].bars == 4);
    CHECK_FALSE(f.ui.execute("LOOP 9 BARS 4"));
    CHECK_FALSE(f.ui.execute("LOOP 1 FROM 1")); // instrument 1 has no sample

    CHECK(f.ui.execute("VAPOR 70"));
    CHECK(s().vapor == 70);
    CHECK_FALSE(f.ui.execute("VAPOR 101"));
    CHECK(f.ui.execute("FX 1 DELAY 5 0.6"));
    CHECK(s().tracks[0].inserts[0].type == model::FxType::Delay);
    CHECK(s().tracks[0].inserts[0].p[0] == 5.0f);
    CHECK(s().tracks[0].inserts[0].p[1] == 0.6f);
    CHECK(f.ui.execute("FX M2 CRUSH"));
    CHECK(s().master_fx[1].type == model::FxType::Crush);
    CHECK(f.ui.execute("FX RB TAPE"));
    CHECK(s().returns[1].type == model::FxType::Tape);
    CHECK(s().returns[1].p[4] == 1.0f); // returns stay all wet
    CHECK_FALSE(f.ui.execute("FX 3 TAPE"));
    CHECK(f.ui.execute("SEND A 40"));
    CHECK(s().tracks[0].send_a == 0.4f);
    CHECK(f.ui.execute("RETURN B 50"));
    CHECK(s().return_level[1] == 0.5f);
    CHECK(f.ui.execute("TRACKTYPE LOOPS"));
    CHECK(s().tracks[0].type == model::TrackType::Loops);
    CHECK(f.ui.execute("LOOP 2 CLEAR"));
    CHECK_FALSE(s().loops[1].sample);

    CHECK(f.ui.execute("UNDO")); // LOOP CLEAR
    CHECK(s().loops[1].sample);
    CHECK_FALSE(f.ui.execute("LOOP 2 PLAY")); // headless: no engine
    const bool went = f.ui.execute("PL");
    INFO(f.ui.message_text());
    CHECK(went);
    CHECK(f.ui.current_page_id() == "L");
}

TEST_CASE("Long INFO messages roll: hold, scroll to the end, hold, start over", "[ui][chrome]") {
    using chrome::roll_offset;
    constexpr int box = 33 * 8;
    CHECK(roll_offset(20 * 8, box, 10.0) == 0); // fits: never moves
    const int text = 60 * 8;                    // 27 characters too long = 216 px of travel
    const int travel = text - box;
    const double scroll_s = travel / chrome::kRollPxPerS; // 4.5 s
    CHECK(roll_offset(text, box, 0.0) == 0);
    CHECK(roll_offset(text, box, chrome::kRollHoldS) == 0);
    CHECK(roll_offset(text, box, chrome::kRollHoldS + 1.0) == 48);
    CHECK(roll_offset(text, box, chrome::kRollHoldS + scroll_s + 0.5) == travel); // end shown and held
    const double cycle = scroll_s + 2.0 * chrome::kRollHoldS;
    CHECK(roll_offset(text, box, chrome::kRollHoldS + scroll_s + chrome::kRollHoldS + 0.1) == 0); // start held again
    CHECK(roll_offset(text, box, chrome::kRollHoldS + cycle + 1.0) == 48);
}

TEST_CASE("The INFO line keeps a long message inside its field", "[ui][chrome]") {
    Fixture f;
    f.ui.message(std::string(80, 'W')); // wider than the field; headless time is fixed
    f.ui.draw(f.canvas);
    // The project / CPU block from column 41 on still shows, not the message.
    bool w_past_field = false;
    for (int y = py(kRowInfo); y < py(kRowInfo) + kCellH; ++y) {
        for (int x = px(40); x < px(41); ++x) {
            w_past_field |= f.canvas.get(x, y) != gfx::kInkField;
        }
    }
    CHECK_FALSE(w_past_field); // column 40 is the gap between message and status
}

namespace {
InputEvent chord_key(char ch, int mods) {
    InputEvent e = key(Key::Char, mods);
    e.ch = ch;
    return e;
}
InputEvent text_ev(char ch) {
    InputEvent t;
    t.type = InputEvent::Type::Text;
    t.ch = ch;
    return t;
}
} // namespace

TEST_CASE("The page key jumps to pages; P goes back; the chosen modifier is a setting", "[ui][keys]") {
    Fixture f;
    REQUIRE(f.ui.execute("PAGEKEY ALT"));
    CHECK(f.ctx.page_modifier() == kModAlt);
    InputState in;
    in.events.push_back(chord_key('K', kModAlt));
    in.events.push_back(text_ev('\xE2')); // Option+K on macOS types a character: dropped
    f.step(in);
    CHECK(f.ui.current_page_id() == "K");
    CHECK(f.ui.command_line().empty());
    in.events.push_back(chord_key('M', kModAlt));
    f.step(in);
    CHECK(f.ui.current_page_id() == "M");
    in.events.push_back(chord_key('P', kModAlt)); // back
    f.step(in);
    CHECK(f.ui.current_page_id() == "K");
    in.events.push_back(chord_key('9', kModAlt)); // no page 9: nothing happens
    f.step(in);
    CHECK(f.ui.current_page_id() == "K");
    in.events.push_back(chord_key('R', kModCtrl)); // other modifier: not the page key
    f.step(in);
    CHECK(f.ui.current_page_id() == "K");

    REQUIRE(f.ui.execute("PAGEKEY OFF"));
    in.events.push_back(chord_key('M', kModAlt));
    f.step(in);
    CHECK(f.ui.current_page_id() == "K");
    CHECK_FALSE(f.ui.execute("PAGEKEY HYPER"));

    // The fixture's key map uses MOD = CTRL: CTRL as page key takes over CTRL+K and CTRL+S.
    REQUIRE(f.ui.execute("PAGEKEY CTRL"));
    REQUIRE(f.ui.execute("PS"));
    f.ui.draw(f.canvas);
    in.events.push_back(chord_key('K', kModCtrl));
    f.step(in);
    CHECK(f.ui.current_page_id() == "K");
    CHECK_FALSE(f.ui.keyjazz());

    Settings s;
    s.page_modifier = kModSuper;
    const auto back = Settings::from_json(s.to_json(), nullptr);
    REQUIRE(back);
    CHECK(back->page_modifier == kModSuper);
    CHECK(Settings::from_json(R"({"schemaVersion":1})", nullptr)->page_modifier == Settings::kDefaultPageModifier);
}

TEST_CASE("Page R AUTO view edits a CC lane; TRACKOUT and MIDIOUT set the MIDI routing", "[ui][m8]") {
    Fixture f;
    auto& ws = f.ctx.workspace();
    REQUIRE(f.ui.execute("PR AUTO 74"));
    CHECK(f.ui.current_page_id() == "R");
    CHECK_FALSE(f.ui.execute("PR SIDEWAYS"));
    REQUIRE(f.ui.execute("PR AUTO 74"));
    // Keyboard: into the grid, set a value on step 3, clear it again.
    InputState in;
    in.events.push_back(key(Key::Down));
    f.step(in);
    f.ui.set_focus(Focus::Fields);
    for (int i = 0; i < 13; ++i) { // header fields down to the grid
        in.events.push_back(key(Key::Down));
    }
    in.events.push_back(key(Key::Right));
    in.events.push_back(key(Key::Right));
    in.events.push_back(key(Key::Up)); // 64 + 1
    in.events.push_back(key(Key::Up, kModShift)); // + 16
    f.step(in);
    const model::AutomationLane* lane = ws.doc().song.pattern().lane(0, 74);
    REQUIRE(lane);
    CHECK(lane->values[2] == 81);
    in.events.push_back(key(Key::Delete));
    f.step(in);
    CHECK(ws.doc().song.pattern().lane(0, 74)->values[2] == -1);

    // Pen: drawing at the top of the chart over the first steps sets 127.
    in.pen_inside = true;
    in.pen_down = true;
    in.pen_pressed = true;
    in.pen_x = AutomationView::kGridX + 2.0f;
    in.pen_y = static_cast<float>(py(7));
    f.step(in);
    CHECK(ws.doc().song.pattern().lane(0, 74)->values[0] == 127);
    in.pen_down = false;
    f.step(in);

    CHECK(f.ui.execute("TRACKOUT EXT CH 10 PROG 33 PORT MY SYNTH"));
    const auto& tr = ws.doc().song.tracks[0];
    CHECK(tr.dest == model::MidiDest::External);
    CHECK(tr.out_channel == 9);
    CHECK(tr.program == 32);
    CHECK(tr.out_port == "MY SYNTH");
    CHECK_FALSE(f.ui.execute("TRACKOUT LOUD"));
    CHECK(f.ui.execute("MIDIOUT ON"));
    CHECK(f.ctx.midi_out_config().enabled);
    CHECK(f.ui.execute("MIDIOUT OFFSET -12"));
    CHECK(f.ctx.midi_out_config().offset_ms == -12);
    CHECK(f.ui.execute("MIDIOUT THRU OFF"));
    CHECK_FALSE(f.ctx.midi_out_config().thru);
    CHECK_FALSE(f.ui.execute("MIDIOUT OFFSET 500"));
    REQUIRE(f.ui.execute("P3 OUT"));
    CHECK(f.ui.current_page_id() == "3");
}

TEST_CASE("FOLLOW: Page R scrolls with the playhead and shows the playing section", "[ui][follow]") {
    Fixture f;
    auto& ws = f.ctx.workspace();
    model::Document doc;
    doc.song.patterns.resize(2);
    doc.song.patterns[0].steps = 64;
    doc.song.sections[1].pattern = 1;
    doc.song.order = {0, 1};
    ws.replace(doc);
    REQUIRE(f.ui.execute("PR"));
    CHECK(f.ctx.follow()); // on by default

    StatusInfo& st = f.ctx.mutable_status();
    st.playing = true;
    st.section = 0;
    st.pattern = 0;
    st.pattern_tick = 40 * 240; // step 41 of 64: the second page (steps 33..64)
    InputState in;
    f.step(in);
    // The cursor moved with the page: ENTER in the grid (where Page R starts) puts a note on step 33.
    f.ui.set_focus(Focus::Fields);
    in.events.push_back(key(Key::Enter));
    f.step(in);
    REQUIRE(ws.doc().song.patterns[0].lanes[0].size() == 1);
    CHECK(ws.doc().song.patterns[0].lanes[0][0].tick == 32 * 240);

    // SONG mode: the page switches to the section that plays.
    model::Song s = ws.doc().song;
    s.play_mode = model::PlayMode::Song;
    ws.set_song(s, "MODE");
    st.section = 1;
    st.pattern = 1;
    st.pattern_tick = 0;
    f.step(in);
    CHECK(ws.doc().song.current_section == 1);

    // FOLLOW OFF: nothing moves.
    REQUIRE(f.ui.execute("FOLLOW OFF"));
    CHECK_FALSE(f.ctx.follow());
    st.section = 0;
    st.pattern = 0;
    f.step(in);
    CHECK(ws.doc().song.current_section == 1);
    REQUIRE(f.ui.execute("FOLLOW"));
    CHECK(f.ctx.follow());
    CHECK_FALSE(f.ui.execute("FOLLOW MAYBE"));
}
