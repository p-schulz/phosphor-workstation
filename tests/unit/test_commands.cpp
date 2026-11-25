#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ui/command_line.h"
#include "ui/commands.h"

using namespace phos::ui;

TEST_CASE("commands parse case-insensitively", "[commands]") {
    const auto c = parse_command("  theme   amber ");
    REQUIRE(c);
    CHECK(c->verb == "THEME");
    REQUIRE(c->args.size() == 1);
    CHECK(c->arg_is(0, "AMBER"));
    CHECK_FALSE(parse_command("   "));
}

TEST_CASE("page verbs", "[commands]") {
    CHECK(page_id_from_verb("P1") == "1");
    CHECK(page_id_from_verb("pr") == "R");
    CHECK(page_id_from_verb("PS") == "S");
    CHECK(page_id_from_verb("PLAY").empty());
    CHECK(page_id_from_verb("PANIC").empty());
    CHECK(page_id_from_verb("P").empty());
}

TEST_CASE("tab completion", "[commands]") {
    CHECK(complete_command("tes") == "TESTCARD ");
    CHECK(complete_command("THEME am") == "THEME AMBER");
    CHECK(complete_command("DISPLAY n") == "DISPLAY NEXT");
    CHECK(complete_command("S") == "S"); // SCALE/SNAP/SHOT/SAVE/SIG/SWING/STOP share only "S"
    CHECK(complete_command("SC") == "SCALE ");
    CHECK(complete_command("xyz") == "xyz");
}

TEST_CASE("command line editing and history", "[commands]") {
    CommandLine cl(10);
    for (char ch : std::string("P1")) {
        cl.insert(ch);
    }
    CHECK(cl.submit() == "P1");
    for (char ch : std::string("PS")) {
        cl.insert(ch);
    }
    cl.submit();
    cl.insert('X');
    cl.history_prev();
    CHECK(cl.text() == "PS");
    cl.history_prev();
    CHECK(cl.text() == "P1");
    cl.history_next();
    CHECK(cl.text() == "PS");
    cl.history_next();
    CHECK(cl.text() == "X"); // draft restored
    CHECK_FALSE(cl.browsing_history());

    cl.clear();
    for (char ch : std::string("ABCDEFGHIJKL")) {
        cl.insert(ch);
    }
    CHECK(cl.text().size() == 10);
    cl.home();
    cl.erase();
    CHECK(cl.text() == "BCDEFGHIJ");
}
