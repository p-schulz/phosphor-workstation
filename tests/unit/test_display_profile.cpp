#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ui/display_profile.h"

using namespace phos::ui;

TEST_CASE("display profiles round-trip through JSON", "[profiles]") {
    DisplayProfiles profiles;
    DisplayProfile pvm;
    pvm.monitor = "PVM CLONE";
    pvm.fullscreen = true;
    pvm.scale = phos::gfx::ScaleMode::Integer;
    pvm.panel_aspect = 4.0 / 3.0;
    pvm.overscan = phos::gfx::Insets{0.02f, 0.02f, 0.01f, 0.03f};
    pvm.theme = "AMBER";
    pvm.crt.curvature = 0.3f;
    profiles.store(pvm);

    std::string error;
    const auto back = DisplayProfiles::from_json(profiles.to_json(), &error);
    REQUIRE(back);
    CHECK(back->to_json() == profiles.to_json());
    const DisplayProfile got = back->for_monitor("PVM CLONE");
    CHECK(got.fullscreen);
    CHECK(got.scale == phos::gfx::ScaleMode::Integer);
    CHECK(got.panel_aspect == Catch::Approx(4.0 / 3.0));
    CHECK(got.overscan.bottom == Catch::Approx(0.03f));
    CHECK(got.theme == "AMBER");
}

TEST_CASE("unknown monitors get the default profile", "[profiles]") {
    DisplayProfiles profiles;
    DisplayProfile def;
    def.theme = "GREY";
    profiles.store(def);
    const auto p = profiles.for_monitor("BUILT-IN RETINA");
    CHECK(p.theme == "GREY");
    CHECK(p.monitor == "BUILT-IN RETINA");
    CHECK_FALSE(profiles.has("BUILT-IN RETINA"));
}

TEST_CASE("profiles reject other schema versions", "[profiles]") {
    std::string error;
    CHECK_FALSE(DisplayProfiles::from_json("{\"schemaVersion\": 99}", &error));
    CHECK(error.find("schemaVersion") != std::string::npos);
    CHECK_FALSE(DisplayProfiles::from_json("not json", &error));
}

TEST_CASE("panel aspect accepts labels and ratios", "[profiles]") {
    std::string error;
    const auto p = DisplayProfiles::from_json("{\"schemaVersion\":1,\"monitors\":["
                                              "{\"monitor\":\"A\",\"panelAspect\":\"16:10\"},"
                                              "{\"monitor\":\"B\",\"panelAspect\":\"3:2\"}]}",
                                              &error);
    REQUIRE(p);
    CHECK(p->for_monitor("A").panel_aspect == Catch::Approx(1.6));
    CHECK(p->for_monitor("B").panel_aspect == Catch::Approx(1.5));
    CHECK(panel_aspect_label(16.0 / 9.0) == "16:9");
}
