#include <catch2/catch_test_macros.hpp>

#include "magda/sdk/display/DisplayList.hpp"

using namespace magda::sdk::display;

TEST_CASE("Display list JSON carries every command", "[display]") {
    DisplayList list(20.0f, 10.0f);
    list.save();
    list.clipRect({1.0f, 1.0f, 18.0f, 8.0f});
    list.fillRect({0.0f, 0.0f, 20.0f, 10.0f}, Colour::of(ColourRole::Background));
    list.strokeRect({0.5f, 0.5f, 19.0f, 9.0f}, Colour::literal(0xFF102030).withAlpha(0.5f), 1.0f,
                    2.0f);
    Path path;
    path.moveTo(0.0f, 10.0f).lineTo(5.0f, 0.0f).quadTo(7.0f, 2.0f, 9.0f, 4.0f);
    path.cubicTo(10.0f, 5.0f, 11.0f, 6.0f, 12.0f, 7.0f).close();
    list.fillPath(path, LinearGradient{{0.0f, 10.0f},
                                       {0.0f, 0.0f},
                                       {{0.0f, Colour::of(ColourRole::MeterLow)},
                                        {1.0f, Colour::of(ColourRole::MeterHigh)}}});
    list.strokePath(path, Colour::of(ColourRole::Accent), 1.5f);
    list.text("-12 \"dB\"", {0.0f, 0.0f, 20.0f, 10.0f}, Colour::of(ColourRole::TextDim), 9.0f,
              Justification::Right);
    list.restore();

    CHECK(toJson(list) ==
          "{\"format\":\"magda.display-list\",\"version\":1,\"width\":20.0,\"height\":10.0,"
          "\"commands\":[\n"
          "{\"op\":\"save\"},\n"
          "{\"op\":\"clipRect\",\"rect\":[1.0,1.0,18.0,8.0]},\n"
          "{\"op\":\"fillRect\",\"rect\":[0.0,0.0,20.0,10.0],"
          "\"paint\":{\"colour\":{\"role\":\"background\"}}},\n"
          "{\"op\":\"strokeRect\",\"rect\":[0.5,0.5,19.0,9.0],\"radius\":2.0,\"lineWidth\":1.0,"
          "\"paint\":{\"colour\":{\"argb\":\"#FF102030\",\"alpha\":0.5}}},\n"
          "{\"op\":\"fillPath\",\"path\":[[\"M\",0.0,10.0],[\"L\",5.0,0.0],"
          "[\"Q\",7.0,2.0,9.0,4.0],[\"C\",10.0,5.0,11.0,6.0,12.0,7.0],[\"Z\"]],"
          "\"paint\":{\"linear\":{\"from\":[0.0,10.0],\"to\":[0.0,0.0],"
          "\"stops\":[[0.0,{\"role\":\"meterLow\"}],[1.0,{\"role\":\"meterHigh\"}]]}}},\n"
          "{\"op\":\"strokePath\",\"path\":[[\"M\",0.0,10.0],[\"L\",5.0,0.0],"
          "[\"Q\",7.0,2.0,9.0,4.0],[\"C\",10.0,5.0,11.0,6.0,12.0,7.0],[\"Z\"]],"
          "\"lineWidth\":1.5,\"paint\":{\"colour\":{\"role\":\"accent\"}}},\n"
          "{\"op\":\"text\",\"text\":\"-12 \\\"dB\\\"\",\"rect\":[0.0,0.0,20.0,10.0],"
          "\"fontSize\":9.0,\"justification\":\"right\",\"colour\":{\"role\":\"textDim\"}},\n"
          "{\"op\":\"restore\"}\n"
          "]}\n");
}

TEST_CASE("Display list JSON rounds to the requested decimals", "[display]") {
    DisplayList list(1.0f / 3.0f, -0.0001f);
    CHECK(toJson(list, {.decimals = 3}) ==
          "{\"format\":\"magda.display-list\",\"version\":1,\"width\":0.333,\"height\":0.0,"
          "\"commands\":[\n]}\n");
}

TEST_CASE("Colour role names round-trip", "[display]") {
    for (int i = 0; i < kNumColourRoles; ++i) {
        const auto role = static_cast<ColourRole>(i);
        CHECK(colourRoleFromName(colourRoleName(role)) == role);
    }
    CHECK_FALSE(colourRoleFromName("nope").has_value());
}
