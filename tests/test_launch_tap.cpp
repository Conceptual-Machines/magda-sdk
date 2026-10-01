#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/sdk/tap/LaunchTap.hpp"

using magda::engine::LaunchTap;

namespace {

/// A quarter beat a block, as the session launcher advances a slot.
constexpr double kBeatsPerBlock = 0.25;

}  // namespace

TEST_CASE("A run days long still moves the playhead", "[tap][launch]") {
    // A run accumulates for as long as the clip plays, and a float's step
    // reaches 16 ms after three days at 120 bpm (#2303 review). The published
    // position has to resolve a block at any age the run reaches.
    LaunchTap tap;

    // Beats covered by thirty-three days at 120 bpm.
    constexpr double kDays = 33.0 * 24.0 * 60.0 * 120.0;
    constexpr double kStep = 1.0 / 4096.0;

    LaunchTap::Reading written;
    written.playing = true;
    written.elapsedBeats = kDays;
    tap.write(written);
    const auto first = tap.read();

    written.elapsedBeats = kDays + kBeatsPerBlock;
    tap.write(written);
    const auto second = tap.read();

    CHECK(first.elapsedBeats == Catch::Approx(kDays).margin(kStep));
    CHECK(second.elapsedBeats - first.elapsedBeats == Catch::Approx(kBeatsPerBlock).margin(kStep));
}

TEST_CASE("A launch tap round-trips its status bits", "[tap][launch]") {
    LaunchTap tap;

    LaunchTap::Reading written;
    written.playing = true;
    written.queued = LaunchTap::Queued::stop;
    written.holdsSection = true;
    written.elapsedBeats = 1.5;
    tap.write(written);

    const auto read = tap.read();
    CHECK(read.playing);
    CHECK(read.queued == LaunchTap::Queued::stop);
    CHECK(read.holdsSection);
    CHECK(read.elapsedBeats == Catch::Approx(1.5));
}

TEST_CASE("A launch tap nobody has written reads as stopped", "[tap][launch]") {
    const LaunchTap tap;

    const auto read = tap.read();
    CHECK_FALSE(read.playing);
    CHECK(read.queued == LaunchTap::Queued::nothing);
    CHECK_FALSE(read.holdsSection);
    CHECK(read.elapsedBeats == 0.0);
}
