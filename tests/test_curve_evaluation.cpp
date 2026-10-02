#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <support/CurveGolden.hpp>
#include <support/CurveHashes.hpp>

#include "magda/sdk/curve/Curve.hpp"

using namespace magda;
using namespace magda::curvecorpus;

namespace {

std::vector<PhaseCase> viaSchema(std::vector<PhaseCase> cases) {
    for (auto& c : cases) {
        sdk::Curve curve;
        curve.points = c.points;
        std::string error;
        const auto text = sdk::writeCurve(curve, error);
        REQUIRE(text.has_value());
        const auto read = sdk::readCurve(*text);
        REQUIRE(read.ok());
        c.points = std::get<std::vector<CurvePointData>>(read.curve->points);
    }
    return cases;
}

std::vector<BeatCase> viaSchema(std::vector<BeatCase> cases) {
    for (auto& c : cases) {
        sdk::Curve curve;
        curve.points = c.points;
        std::string error;
        const auto text = sdk::writeCurve(curve, error);
        REQUIRE(text.has_value());
        const auto read = sdk::readCurve(*text);
        REQUIRE(read.ok());
        c.points = std::get<std::vector<AutomationPoint>>(read.curve->points);
    }
    return cases;
}

void expectGolden(const Hashes& got) {
    for (const auto& [key, hash] : got) {
        INFO(key);
        const auto it = goldenHashes().find(key);
        REQUIRE(it != goldenHashes().end());
#if defined(__APPLE__) && defined(__aarch64__)
        // Captured on this platform; libm and FMA contraction differ elsewhere.
        CHECK(hash == it->second);
#endif
    }
}

}  // namespace

TEST_CASE("the evaluators return what magda-core's evaluators returned, bit for bit",
          "[curve][golden]") {
    Hashes got;
    hashBuiltIns(got);
    for (const auto& c : phaseCases())
        hashPhaseCase(got, c);
    for (const auto& c : beatCases())
        hashBeatCase(got, c);
    for (const auto& c : laneCases())
        hashLaneCase(got, c);
    for (const auto& c : simplifyCases())
        hashSimplifyCase(got, c);

    CHECK(got.size() == goldenHashes().size());
    expectGolden(got);
}

TEST_CASE("a curve read back from its document evaluates bit for bit as before",
          "[curve][golden]") {
    Hashes direct;
    for (const auto& c : phaseCases())
        hashPhaseCase(direct, c);
    for (const auto& c : beatCases())
        hashBeatCase(direct, c);

    Hashes through;
    for (const auto& c : viaSchema(phaseCases()))
        hashPhaseCase(through, c);
    for (const auto& c : viaSchema(beatCases()))
        hashBeatCase(through, c);

    CHECK(through == direct);
    expectGolden(through);
}

TEST_CASE("evaluateCurve reads a curve in its own domain", "[curve]") {
    sdk::Curve phase;
    phase.points = std::vector<CurvePointData>{{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}};
    CHECK(sdk::evaluateCurve(phase, 0.25) == 0.5);
    CHECK(sdk::evaluateCurve(phase, 0.5) == 1.0);

    sdk::Curve beats;
    AutomationPoint a;
    a.id = 1;
    a.beatPosition = 0.0;
    a.value = 0.0;
    AutomationPoint b;
    b.id = 2;
    b.beatPosition = 4.0;
    b.value = 1.0;
    beats.points = std::vector<AutomationPoint>{a, b};
    CHECK(sdk::evaluateCurve(beats, 1.0) == 0.25);
    CHECK(sdk::evaluateCurve(beats, -3.0) == 0.0);
    CHECK(sdk::evaluateCurve(beats, 9.0) == 1.0);
}
