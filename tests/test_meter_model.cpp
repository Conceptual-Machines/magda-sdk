#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/sdk/meter/MeterModel.hpp"
#include "magda/sdk/meter/MeterPainter.hpp"

using magda::sdk::MeterBallistics;
using magda::sdk::MeterLayout;
using magda::sdk::MeterModel;
using magda::sdk::MeterScale;

namespace {

constexpr float kFrameMs = MeterBallistics::kNominalFrameMs;

void setBoth(MeterModel& meter, float gain) {
    const std::array<float, 2> gains{gain, gain};
    meter.setTargets(gains);
}

}  // namespace

TEST_CASE("MeterScale maps dB along the power curve and back", "[meter]") {
    const MeterScale scale;
    CHECK(scale.gainToDb(0.0f) == scale.minDb);
    CHECK(scale.gainToDb(1.0f) == Catch::Approx(0.0f).margin(1e-6));
    CHECK(scale.dbToPosition(-60.0f) == 0.0f);
    CHECK(scale.dbToPosition(6.0f) == 1.0f);
    CHECK(scale.dbToPosition(0.0f) == Catch::Approx(std::pow(60.0f / 66.0f, 3.0f)));
    for (const float db : {-48.0f, -12.0f, -3.0f, 0.0f, 3.0f})
        CHECK(scale.positionToDb(scale.dbToPosition(db)) == Catch::Approx(db).margin(1e-4));
}

TEST_CASE("One 60 Hz frame of attack moves 90% of the way", "[meter]") {
    MeterModel meter;
    setBoth(meter, 0.5f);
    CHECK(meter.advance(kFrameMs));
    CHECK(meter.channel(0).displayGain == Catch::Approx(0.45f));
}

TEST_CASE("Ballistics depend on elapsed time, not on how it is split", "[meter]") {
    MeterModel whole, halves;
    setBoth(whole, 0.8f);
    setBoth(halves, 0.8f);
    whole.advance(kFrameMs);
    halves.advance(kFrameMs / 2.0f);
    halves.advance(kFrameMs / 2.0f);
    CHECK(halves.channel(0).displayGain == Catch::Approx(whole.channel(0).displayGain));

    setBoth(whole, 0.0f);
    setBoth(halves, 0.0f);
    whole.advance(10.0f * kFrameMs);
    for (int i = 0; i < 10; ++i)
        halves.advance(kFrameMs);
    CHECK(halves.channel(0).displayGain == Catch::Approx(whole.channel(0).displayGain));
}

TEST_CASE("A peak holds, then decays to the floor", "[meter]") {
    MeterModel meter;
    setBoth(meter, 1.0f);
    CHECK(meter.channel(0).peakDb == Catch::Approx(0.0f).margin(1e-6));
    setBoth(meter, 0.0f);

    meter.advance(1400.0f);
    CHECK(meter.channel(0).peakDb == Catch::Approx(0.0f).margin(1e-6));
    meter.advance(100.0f);  // the hold runs out
    CHECK(meter.channel(0).peakDb == Catch::Approx(0.0f).margin(1e-6));
    meter.advance(100.0f);
    CHECK(meter.channel(0).peakDb == Catch::Approx(-0.8f * 6.0f));

    for (int i = 0; i < 200 && !meter.isIdle(); ++i)
        meter.advance(kFrameMs);
    CHECK(meter.isIdle());
    CHECK(meter.channel(0).peakDb == meter.scale().minDb);
    CHECK_FALSE(meter.advance(kFrameMs));
}

TEST_CASE("Only a reading over full scale latches the clip flag", "[meter]") {
    MeterModel meter;
    setBoth(meter, 1.0f);
    CHECK_FALSE(meter.anyClipped());

    const std::array<float, 2> over{0.2f, 1.5f};
    meter.setTargets(over);
    CHECK_FALSE(meter.channel(0).clipped);
    CHECK(meter.channel(1).clipped);

    setBoth(meter, 0.0f);
    meter.advance(5000.0f);
    CHECK(meter.channel(1).clipped);
    meter.clearClips();
    CHECK_FALSE(meter.anyClipped());
}

TEST_CASE("Targets are clamped, and a mono reading feeds both channels", "[meter]") {
    MeterModel meter;
    const std::array<float, 1> mono{5.0f};
    meter.setTargets(mono);
    CHECK(meter.channel(0).targetGain == meter.ballistics().maxGain);
    CHECK(meter.channel(1).targetGain == meter.ballistics().maxGain);
}

TEST_CASE("A LevelTap read feeds the model", "[meter]") {
    magda::engine::LevelTap::Levels levels;
    levels.peak = {0.25f, 0.5f};
    MeterModel meter;
    meter.setTargets(levels);
    CHECK(meter.channel(0).targetGain == 0.25f);
    CHECK(meter.channel(1).targetGain == 0.5f);
}

TEST_CASE("The zero-dB anchor pins 0 dB to the requested y", "[meter]") {
    const MeterScale scale;
    MeterLayout layout{10.0f, 100.0f, MeterLayout::Orientation::Vertical, 40.0f};
    CHECK(magda::sdk::meterPosition(scale, layout, 0.0f) == Catch::Approx(0.6f));
    CHECK(magda::sdk::meterPosition(scale, layout, scale.minDb) == 0.0f);
    CHECK(magda::sdk::meterPosition(scale, layout, scale.maxDb) == Catch::Approx(1.0f));

    layout.orientation = MeterLayout::Orientation::Horizontal;
    CHECK(magda::sdk::meterPosition(scale, layout, 0.0f) == scale.dbToPosition(0.0f));
}
