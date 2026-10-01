#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/sdk/lockfree/SidechainTriggerBus.hpp"

using namespace magda;

TEST_CASE("SidechainTriggerBus - MIDI note-on/off counters", "[sidechain][bus]") {
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    SECTION("Initial counters are zero") {
        REQUIRE(bus.getNoteOnCounter(0) == 0);
        REQUIRE(bus.getNoteOffCounter(0) == 0);
    }

    SECTION("Note-on increments counter") {
        bus.triggerNoteOn(0);
        REQUIRE(bus.getNoteOnCounter(0) == 1);
        bus.triggerNoteOn(0);
        REQUIRE(bus.getNoteOnCounter(0) == 2);
    }

    SECTION("Note-off increments counter") {
        bus.triggerNoteOff(0);
        REQUIRE(bus.getNoteOffCounter(0) == 1);
    }

    SECTION("Counters are per-track") {
        bus.triggerNoteOn(0);
        bus.triggerNoteOn(0);
        bus.triggerNoteOn(1);

        REQUIRE(bus.getNoteOnCounter(0) == 2);
        REQUIRE(bus.getNoteOnCounter(1) == 1);
        REQUIRE(bus.getNoteOnCounter(2) == 0);
    }

    SECTION("Invalid track IDs are ignored") {
        bus.triggerNoteOn(-1);
        bus.triggerNoteOff(-1);
        REQUIRE(bus.getNoteOnCounter(-1) == 0);
        REQUIRE(bus.getNoteOffCounter(-1) == 0);
    }

    SECTION("clearAll resets all counters") {
        bus.triggerNoteOn(0);
        bus.triggerNoteOn(1);
        bus.triggerNoteOff(0);
        bus.clearAll();

        REQUIRE(bus.getNoteOnCounter(0) == 0);
        REQUIRE(bus.getNoteOnCounter(1) == 0);
        REQUIRE(bus.getNoteOffCounter(0) == 0);
    }
}

TEST_CASE("SidechainTriggerBus - Audio peak levels", "[sidechain][bus]") {
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    SECTION("Initial peak level is zero") {
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.0f));
    }

    SECTION("Set and get peak level") {
        bus.setAudioPeakLevel(0, 0.75f);
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.75f));
    }

    SECTION("Peak levels are per-track") {
        bus.setAudioPeakLevel(0, 0.5f);
        bus.setAudioPeakLevel(1, 0.9f);

        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.5f));
        REQUIRE(bus.getAudioPeakLevel(1) == Catch::Approx(0.9f));
        REQUIRE(bus.getAudioPeakLevel(2) == Catch::Approx(0.0f));
    }

    SECTION("Peak level overwrites previous value") {
        bus.setAudioPeakLevel(0, 0.8f);
        bus.setAudioPeakLevel(0, 0.2f);
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.2f));
    }

    SECTION("Invalid track ID returns zero") {
        REQUIRE(bus.getAudioPeakLevel(-1) == Catch::Approx(0.0f));
    }

    SECTION("clearAll resets peak levels") {
        bus.setAudioPeakLevel(0, 0.9f);
        bus.clearAll();
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.0f));
    }
}
