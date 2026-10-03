#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "magda/sdk/peaks/PeakData.hpp"

using magda::ConstBufferView;
using namespace magda::sdk;

namespace {

struct Fixture {
    const char* name;
    int channels;
    int length;
    bool floatWav;
    std::uint64_t payloadHash;
};

/// Noise whose amplitude steps down every bucket, with the 16-bit extremes pinned at both ends.
std::vector<std::int16_t> fixtureChannel(int channel, int length) {
    std::vector<std::int16_t> out(static_cast<std::size_t>(length));
    std::uint32_t s = 12345u + static_cast<std::uint32_t>(channel) * 7919u;
    for (int i = 0; i < length; ++i) {
        s = s * 1664525u + 1013904223u;
        out[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(s >> 16)) >> ((i / 64) % 8));
    }
    out.back() = 32767;
    if (channel == 0)
        out.front() = -32768;
    return out;
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t h = 1469598103934665603ull;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

/// The bytes magda-core writes after its cache header: per channel, little-endian min, max pairs.
std::vector<std::uint8_t> payloadBytes(const PeakData& data) {
    std::vector<std::uint8_t> bytes;
    for (int ch = 0; ch < data.numChannels(); ++ch)
        for (auto v : data.channelPeaks(ch)) {
            const auto u = static_cast<std::uint16_t>(v);
            bytes.push_back(static_cast<std::uint8_t>(u & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>(u >> 8));
        }
    return bytes;
}

PeakData build(const Fixture& f, int blockSamples) {
    std::vector<std::vector<float>> channels;
    const float scale = f.floatWav ? 1.0f / 16384.0f : 1.0f / 32768.0f;
    for (int ch = 0; ch < f.channels; ++ch) {
        std::vector<float> samples;
        for (auto v : fixtureChannel(ch, f.length))
            samples.push_back(static_cast<float>(v) * scale);
        channels.push_back(std::move(samples));
    }

    PeakData data(f.channels, f.length);
    for (int start = 0; start < f.length; start += blockSamples) {
        const int frames = std::min(blockSamples, f.length - start);
        std::vector<const float*> pointers;
        for (const auto& c : channels)
            pointers.push_back(c.data() + start);
        REQUIRE(data.addBlock(ConstBufferView(pointers.data(), f.channels, frames), start));
    }
    return data;
}

}  // namespace

TEST_CASE("Peaks are byte-identical to magda-core's cache payload", "[peaks]") {
    // Hashes captured from magda-core's WaveformPeakCache before it moved here.
    static const Fixture kFixtures[] = {
        {"mono_1", 1, 1, false, 4368492281661021131ull},
        {"mono_63", 1, 63, false, 4453476833923492929ull},
        {"mono_64", 1, 64, false, 4453476833923492929ull},
        {"mono_65", 1, 65, false, 9301015227453570985ull},
        {"mono_1000", 1, 1000, false, 9983402623296114481ull},
        {"stereo_4097", 2, 4097, false, 429778391380076314ull},
        {"stereo_65536", 2, 65536, false, 1080836212127174483ull},
        {"stereo_65537", 2, 65537, false, 8666968538843584175ull},
        {"stereo_131201", 2, 131201, false, 3670888310760834335ull},
        {"tri_5000", 3, 5000, false, 1718059950880015234ull},
        {"float_mono_100003", 1, 100003, true, 4144862335892596701ull},
        {"float_stereo_70001", 2, 70001, true, 12213278077523292931ull},
    };

    for (const auto& f : kFixtures) {
        INFO(f.name);
        // The block size must not change the result, only where a block ends.
        for (int blockSamples : {64, 64 * 3, 64 * 1024, 1 << 30}) {
            INFO("block " << blockSamples);
            CHECK(fnv1a(payloadBytes(build(f, blockSamples))) == f.payloadHash);
        }
    }
}

TEST_CASE("A peak quantises with a clamp and truncation", "[peaks]") {
    CHECK(floatToPeak(1.0f) == 32767);
    CHECK(floatToPeak(-1.0f) == -32767);
    CHECK(floatToPeak(3.0f) == 32767);
    CHECK(floatToPeak(-3.0f) == -32767);
    CHECK(floatToPeak(0.5f) == 16383);
    CHECK(floatToPeak(-0.5f) == -16383);
    CHECK(peakToFloat(32767) == 1.0f);
}

TEST_CASE("A bucket of NaN samples is silence", "[peaks]") {
    PeakData data(1, 64);
    std::vector<float> samples(64, std::numeric_limits<float>::quiet_NaN());
    const float* pointers[] = {samples.data()};
    REQUIRE(data.addBlock(ConstBufferView(pointers, 1, 64), 0));
    CHECK(data.channelPeaks(0)[0] == 0);
    CHECK(data.channelPeaks(0)[1] == 0);
}

TEST_CASE("addBlock refuses a block that does not fit", "[peaks]") {
    PeakData data(2, 200);
    std::vector<float> samples(128, 0.5f);
    const float* both[] = {samples.data(), samples.data()};
    const float* one[] = {samples.data()};

    CHECK_FALSE(data.addBlock(ConstBufferView(one, 1, 64), 0));
    CHECK_FALSE(data.addBlock(ConstBufferView(both, 2, 64), 10));
    CHECK_FALSE(data.addBlock(ConstBufferView(both, 2, 128), 128));
    CHECK_FALSE(data.addBlock(ConstBufferView(both, 2, 64), -64));
    CHECK(data.channelPeaks(0)[0] == 0);
    CHECK(data.addBlock(ConstBufferView(both, 2, 64), 128));
    CHECK(data.channelPeaks(1)[2 * 2] == floatToPeak(0.5f));
}

TEST_CASE("getMinMaxForRange spans buckets and clamps its range", "[peaks]") {
    PeakData data(2, 200);
    std::vector<float> ch0(200, 0.0f);
    std::vector<float> ch1(200, 0.25f);
    ch0[10] = -0.5f;
    ch0[130] = 0.75f;
    const float* pointers[] = {ch0.data(), ch1.data()};
    REQUIRE(data.addBlock(ConstBufferView(pointers, 2, 200), 0));

    CHECK(data.numChannels() == 2);
    CHECK(data.numSourceSamples() == 200);
    CHECK(data.numBuckets() == 4);

    const auto all = data.getMinMaxForRange(0, 0, 200);
    CHECK(all.min == Catch::Approx(-0.5f).margin(1e-4));
    CHECK(all.max == Catch::Approx(0.75f).margin(1e-4));

    // A range inside the third bucket sees only that bucket.
    const auto mid = data.getMinMaxForRange(0, 128, 129);
    CHECK(mid.min == 0.0f);
    CHECK(mid.max == Catch::Approx(0.75f).margin(1e-4));

    const auto clamped = data.getMinMaxForRange(0, -50, 100000);
    CHECK(clamped.min == all.min);
    CHECK(clamped.max == all.max);

    CHECK(data.getMinMaxForRange(1, 0, 200).max == Catch::Approx(0.25f).margin(1e-4));
    CHECK(data.getMinMaxForRange(2, 0, 200).max == 0.0f);
    CHECK(data.getMinMaxForRange(-1, 0, 200).max == 0.0f);
    CHECK(data.getMinMaxForRange(0, 50, 50).max == 0.0f);
    CHECK(data.getMinMaxForRange(0, 60, 20).max == 0.0f);
}

TEST_CASE("fromPacked rebuilds stored buckets and rejects a wrong size", "[peaks]") {
    PeakData built(1, 130);
    std::vector<float> samples(130, 0.5f);
    const float* pointers[] = {samples.data()};
    REQUIRE(built.addBlock(ConstBufferView(pointers, 1, 130), 0));

    const auto span = built.channelPeaks(0);
    auto rebuilt = PeakData::fromPacked(130, {{span.begin(), span.end()}});
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->numBuckets() == 3);
    CHECK(rebuilt->getMinMaxForRange(0, 0, 130).max == built.getMinMaxForRange(0, 0, 130).max);

    CHECK_FALSE(PeakData::fromPacked(130, {}).has_value());
    CHECK_FALSE(PeakData::fromPacked(130, {{0, 0}}).has_value());
    CHECK_FALSE(PeakData::fromPacked(-1, {{}}).has_value());
}
