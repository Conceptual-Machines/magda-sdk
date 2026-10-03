#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <magda/sdk/audio/BlockPeak.hpp>
#include <vector>

using magda::sdk::peakMagnitude;

TEST_CASE("peakMagnitude is the highest absolute sample", "[peak]") {
    for (const int length : {1, 7, 8, 9, 31, 64, 1000}) {
        std::vector<float> block(static_cast<std::size_t>(length), 0.1f);
        block.back() = -0.9f;
        block.front() = 0.4f;
        INFO("length " << length);
        CHECK(peakMagnitude(block.data(), length) == (length == 1 ? 0.4f : 0.9f));
    }
}

TEST_CASE("peakMagnitude skips NaN and counts infinity", "[peak]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    std::vector<float> block(20, 0.25f);
    block[3] = nan;
    block[18] = nan;
    CHECK(peakMagnitude(block.data(), 20) == 0.25f);

    block[10] = -inf;
    CHECK(peakMagnitude(block.data(), 20) == inf);
}

TEST_CASE("peakMagnitude of nothing is zero", "[peak]") {
    CHECK(peakMagnitude(nullptr, 10) == 0.0f);
    const float sample = 1.0f;
    CHECK(peakMagnitude(&sample, 0) == 0.0f);
}
