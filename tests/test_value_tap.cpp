#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "magda/sdk/tap/ValueTap.hpp"

using magda::engine::ValueTap;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-6);
}

}  // namespace

TEST_CASE("A value tap holds what the last block published", "[engine][tap][value]") {
    ValueTap tap;
    tap.write(0.25f);
    tap.write(0.75f);

    CHECK(tap.value() == approx(0.75f));
}

TEST_CASE("Reading a value tap does not consume it", "[engine][tap][value]") {
    ValueTap tap;
    tap.write(0.4f);

    // The difference between this and a meter, and the reason it is not one: a
    // position is where the value is, so two readers are owed the same answer
    // and a reader arriving late is owed the current one rather than a zero.
    CHECK(tap.read().value == approx(0.4f));
    CHECK(tap.read().value == approx(0.4f));
    CHECK(tap.value() == approx(0.4f));
}

TEST_CASE("A value tap counts the blocks that published it", "[engine][tap][value]") {
    ValueTap tap;
    REQUIRE(tap.read().writes == 0);

    tap.write(0.5f);
    const auto first = tap.read().writes;

    // The same value again is still a block: what the count answers is whether
    // anything is rendering, and a tap that fell silent on a value that had
    // stopped moving would be indistinguishable from one whose engine had gone
    // away.
    tap.write(0.5f);
    const auto second = tap.read().writes;

    CHECK(first == 1);
    CHECK(second == 2);
    CHECK(tap.value() == approx(0.5f));
}

TEST_CASE("A value tap's count belongs to the value beside it", "[engine][tap][value]") {
    ValueTap tap;

    // The pair is what a reader gates a repaint on, so the two halves coming
    // from different blocks is not a stale frame: a count that has moved past
    // the value it arrived with means the reader draws the older one and then
    // decides, at the next poll, that nothing has changed since. The value it
    // never drew would stay undrawn until something else wrote.
    for (std::uint32_t written = 1; written <= 8; ++written) {
        tap.write(static_cast<float>(written) / 8.0f);

        const auto reading = tap.read();
        CHECK(reading.writes == written);
        CHECK(reading.value == approx(static_cast<float>(reading.writes) / 8.0f));
    }
}

TEST_CASE("A value tap's count wraps past its maximum without passing through zero",
          "[engine][tap][value]") {
    // Zero is the reading that says nothing in the engine publishes this value
    // and the model's own is the answer, so a count that reached it by counting
    // would hand a host a live parameter wearing that sign. Thirty-three days
    // at 96 kHz and 64 samples a block gets there, which is a rig left running
    // rather than a hypothetical, and is not a number a test can render its way
    // to: the rule is asserted where it is decided.
    STATIC_REQUIRE(ValueTap::nextWriteCount(0) == 1);
    STATIC_REQUIRE(ValueTap::nextWriteCount(41) == 42);
    STATIC_REQUIRE(ValueTap::nextWriteCount(0xFFFFFFFEU) == 0xFFFFFFFFU);
    STATIC_REQUIRE(ValueTap::nextWriteCount(0xFFFFFFFFU) == 1);
}

TEST_CASE("A value tap that has published nothing reads as nothing", "[engine][tap][value]") {
    const ValueTap tap;

    const auto reading = tap.read();
    CHECK(reading.writes == 0);
    CHECK(reading.value == approx(0.0f));
}
