#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <set>

#include "magda/sdk/dsp/Random.hpp"

using magda::sdk::Lcg48Random;
using magda::sdk::SplitMix64;
using magda::sdk::Xoshiro256;

TEST_CASE("Lcg48Random follows the 48-bit recurrence", "[dsp][random]") {
    Lcg48Random r(42);
    REQUIRE(r.nextInt() == 16159453);
    REQUIRE(r.nextInt() == -1281479697);

    std::int64_t seed = -7;
    Lcg48Random s(-7);
    for (int i = 0; i < 1000; ++i) {
        seed = static_cast<std::int64_t>(
            ((static_cast<std::uint64_t>(seed) * 0x5deece66dULL) + 11) & 0xffffffffffffULL);
        REQUIRE(s.nextInt() == static_cast<int>(seed >> 16));
    }
}

TEST_CASE("Lcg48Random ranges stay in bounds", "[dsp][random]") {
    Lcg48Random r(1);
    for (int i = 0; i < 10000; ++i) {
        const int v = r.nextInt(7);
        REQUIRE(v >= 0);
        REQUIRE(v < 7);
        const int w = r.nextInt(-3, 5);
        REQUIRE(w >= -3);
        REQUIRE(w < 2);
        const float f = r.nextFloat();
        REQUIRE(f >= 0.0f);
        REQUIRE(f < 1.0f);
        const double d = r.nextDouble();
        REQUIRE(d >= 0.0);
        REQUIRE(d < 1.0);
    }
}

TEST_CASE("Lcg48Random setSeed restarts the sequence", "[dsp][random]") {
    Lcg48Random r(5);
    const int first = r.nextInt();
    r.nextInt();
    r.setSeed(5);
    REQUIRE(r.nextInt() == first);
}

TEST_CASE("SplitMix64 matches its reference stream", "[dsp][random]") {
    SplitMix64 m(1234567);
    REQUIRE(m.next() == 6457827717110365317ULL);
    REQUIRE(m.next() == 3203168211198807973ULL);
}

TEST_CASE("Xoshiro256 is deterministic per seed and bounded", "[dsp][random]") {
    Xoshiro256 a(99), b(99), c(100);
    std::set<std::uint64_t> seen;
    for (int i = 0; i < 1000; ++i) {
        const auto v = a.next();
        REQUIRE(v == b.next());
        seen.insert(v);
    }
    REQUIRE(seen.size() == 1000);
    REQUIRE(Xoshiro256(99).next() != c.next());

    Xoshiro256 r(7);
    for (int i = 0; i < 10000; ++i) {
        REQUIRE(r.nextBelow(10) < 10u);
        const double d = r.nextDouble();
        REQUIRE((d >= 0.0 && d < 1.0));
        const float f = r.nextFloat();
        REQUIRE((f >= 0.0f && f < 1.0f));
    }
}
