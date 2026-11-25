#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ui/key_map.h"

using namespace phos::ui;

TEST_CASE("chords parse with platform MOD", "[keys]") {
    const auto mac = parse_chord("MOD+SHIFT+D", true);
    REQUIRE(mac);
    CHECK(mac->key == Key::Char);
    CHECK(mac->ch == 'D');
    CHECK(mac->mods == (kModSuper | kModShift));
    const auto win = parse_chord("mod+s", false);
    REQUIRE(win);
    CHECK(win->mods == kModCtrl);
    CHECK(parse_chord("F12", false)->key == Key::F12);
    CHECK(parse_chord("ALT+ENTER", false)->key == Key::Enter);
    CHECK_FALSE(parse_chord("HYPER+X", false));
    CHECK(chord_name(*mac) == "CMD+SHIFT+D");
}

TEST_CASE("default bindings", "[keys]") {
    const KeyMap map = KeyMap::defaults(false);
    REQUIRE(map.command_for(Key::F1));
    CHECK(*map.command_for(Key::F1) == "P1");
    CHECK(*map.command_for(Key::F12) == "PR");
    InputEvent ev;
    ev.key = Key::Char;
    ev.ch = 'D';
    ev.mods = kModCtrl | kModShift;
    REQUIRE(map.lookup(ev));
    CHECK(*map.lookup(ev) == "DISPLAY NEXT");
    ev.mods = kModCtrl;
    CHECK_FALSE(map.lookup(ev));
}

TEST_CASE("key file parsing", "[keys]") {
    std::string error;
    const auto map = KeyMap::parse("# remote\nUP = P1\n= = THEME AMBER\n", false, &error);
    REQUIRE(map);
    CHECK(map->size() == 2);
    CHECK(*map->command_for(Key::Up) == "P1");
    CHECK_FALSE(KeyMap::parse("F1 P1\n", false, &error));
    CHECK_FALSE(error.empty());
    CHECK(default_keys_text().find("F1 = P1") != std::string_view::npos);
}
