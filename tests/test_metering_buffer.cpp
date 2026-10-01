#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>

#include "magda/sdk/lockfree/MeteringBuffer.hpp"

using magda::MeterData;
using magda::MeteringBuffer;
using magda::RMSAccumulator;

TEST_CASE("A metering buffer hands levels back in order", "[metering]") {
    MeteringBuffer buffer;

    MeterData first;
    first.peakL = 0.25f;
    MeterData second;
    second.peakL = 0.5f;
    REQUIRE(buffer.pushLevels(3, first));
    REQUIRE(buffer.pushLevels(3, second));

    MeterData out;
    REQUIRE(buffer.popLevels(3, out));
    CHECK(out.peakL == Catch::Approx(0.25f));
    REQUIRE(buffer.popLevels(3, out));
    CHECK(out.peakL == Catch::Approx(0.5f));
    CHECK_FALSE(buffer.popLevels(3, out));
}

TEST_CASE("A metering buffer drains to the latest and keeps tracks apart", "[metering]") {
    MeteringBuffer buffer;

    for (auto i = 1; i <= 4; ++i) {
        MeterData data;
        data.peakL = static_cast<float>(i) * 0.1f;
        REQUIRE(buffer.pushLevels(0, data));
    }

    MeterData out;
    CHECK_FALSE(buffer.peekLatest(1, out));
    REQUIRE(buffer.drainToLatest(0, out));
    CHECK(out.peakL == Catch::Approx(0.4f));
    CHECK_FALSE(buffer.drainToLatest(0, out));
}

TEST_CASE("A metering buffer refuses a full ring and an out of range track", "[metering]") {
    MeteringBuffer buffer;
    const MeterData data;

    for (auto i = 0; i < MeteringBuffer::kBufferSize - 1; ++i)
        REQUIRE(buffer.pushLevels(0, data));
    CHECK_FALSE(buffer.pushLevels(0, data));

    CHECK_FALSE(buffer.pushLevels(-1, data));
    CHECK_FALSE(buffer.pushLevels(MeteringBuffer::kMaxTracks, data));
}

TEST_CASE("A metering buffer delivers every level across two threads", "[metering][thread]") {
    MeteringBuffer buffer;
    constexpr int kLevels = 20000;

    std::thread writer([&] {
        for (auto i = 1; i <= kLevels; ++i) {
            MeterData data;
            data.peakL = static_cast<float>(i);
            while (!buffer.pushLevels(2, data)) {
            }
        }
    });

    auto expected = 1;
    while (expected <= kLevels) {
        MeterData out;
        if (buffer.popLevels(2, out)) {
            REQUIRE(out.peakL == static_cast<float>(expected));
            ++expected;
        }
    }

    writer.join();
}

TEST_CASE("An RMS accumulator reports the root mean square of a window", "[metering]") {
    RMSAccumulator rms(4);
    CHECK(rms.getRMSL() == 0.0f);

    const float left[4] = {1.0f, -1.0f, 1.0f, -1.0f};
    rms.addBlock(left, nullptr, 4);

    CHECK(rms.isWindowComplete());
    CHECK(rms.getRMSL() == Catch::Approx(1.0f));
    CHECK(rms.getRMSR() == Catch::Approx(0.0f));

    rms.reset();
    CHECK(rms.getSampleCount() == 0);
}
