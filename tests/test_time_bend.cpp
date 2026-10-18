#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <magda/sdk/sequencer/TimeBend.hpp>
#include <vector>

using namespace magda::sdk::sequencer;
using Catch::Approx;

namespace {

constexpr double kStep = 0.25;

struct Hit {
    double real;
    long long step;
};

/// Sixteenth steps on an endless grid: the step whose pattern beat is k * kStep.
auto sixteenths() {
    return [](double lo, double hi, const auto& visit) {
        for (auto k = static_cast<long long>(std::floor(lo / kStep)) - 1;
             static_cast<double>(k) * kStep <= hi + kStep; ++k)
            visit(static_cast<double>(k) * kStep, k);
    };
}

std::vector<Hit> hits(const TimeBend& bend, double cycleBeats, double t0, double t1) {
    std::vector<Hit> out;
    forEachBentHit(bend, cycleBeats, t0, t1, sixteenths(),
                   [&](double real, long long step) { out.push_back({real, step}); });
    return out;
}

TimeBend shaped(TimeBendShape shape) {
    auto bend = makeTimeBend(shape);
    bend.on = true;
    return bend;
}

/// Rhythmica's double-precision segment, to pin the knot encoding against.
double rhythmicaSegment(double y1, double y2, double bend, double t) {
    const double dy = y2 - y1;
    if (std::abs(dy) < 1.0e-9)
        return y1;
    const double control = 0.5 * (y1 + y2) + 0.5 * bend;
    const double raw = ((0.25 * y1) + (0.5 * control) + (0.25 * y2) - y1) / dy;
    constexpr double kEps = 1.0e-4;
    const double r = raw <= 0.0   ? kEps * std::exp(raw * 5.0)
                     : raw >= 1.0 ? 1.0 - kEps * std::exp((1.0 - raw) * 5.0)
                                  : std::clamp(raw, kEps, 1.0 - kEps);
    const double inv = 1.0 / std::log(0.5);
    const double warp = r <= 0.5 ? std::pow(t, std::log(r) * inv)
                                 : 1.0 - std::pow(1.0 - t, std::log(1.0 - r) * inv);
    return y1 + dy * warp;
}

}  // namespace

TEST_CASE("An inactive bend plays every step where it is written", "[timebend]") {
    for (const auto& bend :
         {TimeBend{}, shaped(TimeBendShape::Straight), TimeBend{.on = true, .amount = 0.0f}}) {
        REQUIRE_FALSE(bend.isActive());
        const auto played = hits(bend, 4.0, 0.0, 4.0);
        REQUIRE(played.size() == 16);
        for (std::size_t i = 0; i < played.size(); ++i) {
            REQUIRE(played[i].step == static_cast<long long>(i));
            REQUIRE(played[i].real == static_cast<double>(i) * kStep);
        }
    }
}

TEST_CASE("Rush plays each step once, earlier than written", "[timebend]") {
    const auto played = hits(shaped(TimeBendShape::Rush), 4.0, 0.0, 4.0);
    REQUIRE(played.size() == 16);
    for (std::size_t i = 0; i < played.size(); ++i) {
        REQUIRE(played[i].step == static_cast<long long>(i));
        REQUIRE(played[i].real <= static_cast<double>(i) * kStep + 1.0e-9);
    }
    REQUIRE(played[8].real < 2.0 - 0.1);
}

TEST_CASE("Hold then catch up is silent, then plays the whole cycle in its second half",
          "[timebend]") {
    const auto played = hits(shaped(TimeBendShape::HoldThenCatchUp), 4.0, 0.0, 4.0);
    REQUIRE(played.size() == 16);
    REQUIRE(played[0].step == 0);
    for (std::size_t i = 1; i < played.size(); ++i)
        REQUIRE(played[i].real >= 2.0);
}

TEST_CASE("Backwards reads the cycle in reverse", "[timebend]") {
    const auto played = hits(shaped(TimeBendShape::Backwards), 4.0, 0.0, 4.0);
    REQUIRE(played.size() == 16);
    for (std::size_t i = 1; i < played.size(); ++i) {
        REQUIRE(played[i].step == played[i - 1].step - 1);
        REQUIRE(played[i].real > played[i - 1].real);
    }
}

TEST_CASE("There and back plays the steps on the way up and again on the way down", "[timebend]") {
    const auto played = hits(shaped(TimeBendShape::ThereAndBack), 4.0, 0.0, 4.0);
    // The turn reads up to the next cycle's downbeat, step 16, which a 16-step pattern plays as 0.
    std::vector<int> times(16, 0);
    for (const auto& hit : played) {
        REQUIRE(hit.step >= 0);
        REQUIRE(hit.step <= 16);
        ++times[static_cast<std::size_t>(hit.step % 16)];
    }
    for (std::size_t step = 0; step < 16; ++step)
        REQUIRE(times[step] == 2);
}

TEST_CASE("Hits do not depend on where a block starts", "[timebend]") {
    auto bend = shaped(TimeBendShape::RushThenDrag);
    bend.cycles = 3;
    const auto whole = hits(bend, bend.cycleBeats(4.0), 0.0, 16.0);

    std::vector<Hit> pieces;
    double t = 0.0;
    for (int i = 0; t < 16.0; ++i) {
        const double next = std::min(16.0, t + 0.013 + 0.07 * static_cast<double>(i % 7));
        for (const auto& hit : hits(bend, bend.cycleBeats(4.0), t, next))
            pieces.push_back(hit);
        t = next;
    }

    REQUIRE(pieces.size() == whole.size());
    for (std::size_t i = 0; i < whole.size(); ++i) {
        REQUIRE(pieces[i].step == whole[i].step);
        REQUIRE(pieces[i].real == whole[i].real);
    }
}

TEST_CASE("Amount blends the diagonal into the curve", "[timebend]") {
    auto bend = shaped(TimeBendShape::HoldThenCatchUp);
    REQUIRE(bend.read(0.25) == Approx(0.0).margin(1.0e-6));
    bend.amount = 0.5f;
    REQUIRE(bend.read(0.25) == Approx(0.125).margin(1.0e-6));
}

TEST_CASE("The cycle is the span in bars over the cycles", "[timebend]") {
    TimeBend bend;
    bend.bars = 2;
    bend.cycles = 3;
    REQUIRE(bend.cycleBeats(4.0) == Approx(8.0 / 3.0));
    REQUIRE(bend.cycleBeats(3.0) == Approx(2.0));
    bend.bars = 3;
    bend.sanitise();
    REQUIRE(bend.bars == 4);
}

TEST_CASE("The read head restarts every cycle", "[timebend]") {
    const auto bend = shaped(TimeBendShape::HoldThenCatchUp);
    REQUIRE(readHeadBeat(bend, 4.0, 1.0) == Approx(4.0 * 0.0).margin(1.0e-6));
    REQUIRE(readHeadBeat(bend, 4.0, 5.0) == Approx(4.0).margin(1.0e-6));
    REQUIRE(readHeadBeat(bend, 4.0, 7.0) == Approx(6.0).margin(1.0e-5));
    REQUIRE(readHeadBeat(TimeBend{}, 4.0, 7.0) == 7.0);
}

TEST_CASE("A knot's bend reads as Rhythmica's segment", "[timebend]") {
    for (const float b : {-1.0f, -0.6f, -0.2f, 0.3f, 0.6f, 1.0f}) {
        const TimeBendKnot knots[] = {{0.0f, 0.0f, b}, {0.5f, 0.7f, -b}, {1.0f, 1.0f}};
        auto bend = makeTimeBend(knots);
        bend.on = true;
        for (int i = 0; i <= 64; ++i) {
            const double x = i / 64.0;
            const double expected = x < 0.5 ? rhythmicaSegment(0.0, 0.7f, b, x / 0.5)
                                            : rhythmicaSegment(0.7f, 1.0, -b, (x - 0.5) / 0.5);
            REQUIRE(bend.read(x) == Approx(expected).margin(1.0e-6));
        }
    }
}

TEST_CASE("A bend survives its state node", "[timebend]") {
    auto bend = shaped(TimeBendShape::RushThenDrag);
    bend.amount = 0.75f;
    bend.bars = 4;
    bend.cycles = 6;
    bend.points[1].curveType = static_cast<int>(magda::sdk::CurveInterpolation::HardCorner);

    const auto read = readTimeBend(writeTimeBend(bend));
    REQUIRE(read.has_value());
    REQUIRE(*read == bend);
    REQUIRE_FALSE(readTimeBend(magda::sdk::StateNode{"Other"}).has_value());
}

TEST_CASE("A bend read from bad state is pinned and clamped", "[timebend]") {
    magda::sdk::StateNode node{"TimeBend"};
    node.setDouble("amount", 7.0);
    node.setInt("bars", 9);
    node.setInt("cycles", 0);
    for (const auto& [x, y] : {std::pair{0.3, -2.0}, {0.1, 0.5}, {0.8, 3.0}}) {
        magda::sdk::StateNode point{"Point"};
        point.setDouble("x", x);
        point.setDouble("y", y);
        point.setInt("type", static_cast<int>(magda::sdk::CurveInterpolation::Step));
        node.addChild(std::move(point));
    }

    const auto bend = readTimeBend(node);
    REQUIRE(bend.has_value());
    REQUIRE(bend->amount == 1.0f);
    REQUIRE(bend->bars == 4);
    REQUIRE(bend->cycles == 1);
    REQUIRE(bend->count == 3);
    REQUIRE(bend->points[0].phase == 0.0f);
    REQUIRE(bend->points[1].phase >= bend->points[0].phase);
    REQUIRE(bend->points[2].phase == 1.0f);
    REQUIRE(bend->points[0].value == 0.0f);
    REQUIRE(bend->points[2].value == 1.0f);
    for (int i = 0; i < 3; ++i)
        REQUIRE(bend->points[static_cast<std::size_t>(i)].curveType ==
                static_cast<int>(magda::sdk::CurveInterpolation::Linear));
}

TEST_CASE("A single point keeps the diagonal", "[timebend]") {
    magda::sdk::StateNode node{"TimeBend"};
    node.setBool("on", true);
    magda::sdk::StateNode point{"Point"};
    point.setDouble("x", 0.5);
    point.setDouble("y", 0.2);
    node.addChild(std::move(point));

    const auto bend = readTimeBend(node);
    REQUIRE(bend.has_value());
    REQUIRE(bend->count == 2);
    REQUIRE_FALSE(bend->isActive());
}

TEST_CASE("A bend survives its text", "[timebend]") {
    auto bend = shaped(TimeBendShape::ThereAndBack);
    bend.amount = 0.3f;
    bend.bars = 2;
    bend.cycles = 5;
    bend.points[0].outHandleY = 0.123456789f;
    bend.points[1].curveType = static_cast<int>(magda::sdk::CurveInterpolation::HardCorner);

    const auto read = timeBendFromText(timeBendToText(bend));
    REQUIRE(read.has_value());
    REQUIRE(*read == bend);
    REQUIRE(timeBendFromText(timeBendToText(TimeBend{})) == TimeBend{});

    for (const auto* bad : {"", "1|x|1|1|", "1|1|1|1|0,0,0,0,0,0,0", "1|1|1|1|0,0,0,0,0,0,0,nan;"})
        REQUIRE_FALSE(timeBendFromText(bad).has_value());
}
