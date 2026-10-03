#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "magda/sdk/dsp/Biquad.hpp"

using magda::sdk::Biquad;
using magda::sdk::BiquadCoeffs;
using magda::sdk::biquadMagnitude;
namespace bq = magda::sdk::biquad;

namespace {

constexpr double kFs = 48000.0;

double db(double linear) {
    return 20.0 * std::log10(linear);
}

}  // namespace

TEST_CASE("Biquad processes the TDF-II difference equation", "[dsp][biquad]") {
    const BiquadCoeffs<double> c{0.2, 0.3, 0.1, -0.5, 0.25};
    Biquad<double> filter(c);

    // Direct form I reference: y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2].
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int n = 0; n < 64; ++n) {
        const double x = std::sin(0.37 * n) + (n == 0 ? 1.0 : 0.0);
        const double ref = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        REQUIRE(filter.process(x) == Catch::Approx(ref).margin(1e-12));
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = ref;
    }
}

TEST_CASE("Biquad reset clears the registers and keeps the coefficients", "[dsp][biquad]") {
    Biquad<float> filter(bq::lowPass(kFs, 1000.0).cast<float>());
    const float first = filter.process(1.0f);
    filter.process(0.5f);
    filter.reset();
    REQUIRE(filter.process(1.0f) == first);
}

TEST_CASE("Low-pass and high-pass sit at -3 dB at the cutoff", "[dsp][biquad]") {
    const auto lp = bq::lowPass(kFs, 2000.0);
    const auto hp = bq::highPass(kFs, 2000.0);
    REQUIRE(db(biquadMagnitude(lp, kFs, 2000.0)) == Catch::Approx(-3.0103).margin(0.01));
    REQUIRE(db(biquadMagnitude(hp, kFs, 2000.0)) == Catch::Approx(-3.0103).margin(0.01));
    REQUIRE(biquadMagnitude(lp, kFs, 20.0) == Catch::Approx(1.0).margin(1e-3));
    REQUIRE(biquadMagnitude(hp, kFs, 20000.0) == Catch::Approx(1.0).margin(0.02));
    REQUIRE(db(biquadMagnitude(lp, kFs, 16000.0)) < -35.0);
    REQUIRE(db(biquadMagnitude(hp, kFs, 200.0)) < -35.0);
}

TEST_CASE("BS.1770 K-weighting stages hit their published gains", "[dsp][biquad]") {
    const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const auto shelf = bq::highShelfK(kFs, 1681.974450955533, vh, vb, 0.7071752369554196);
    const auto hp = bq::highPassK(kFs, 38.13547087602444, 0.5003270373238773);
    REQUIRE(db(biquadMagnitude(shelf, kFs, 20.0)) == Catch::Approx(0.0).margin(0.01));
    REQUIRE(db(biquadMagnitude(shelf, kFs, 20000.0)) == Catch::Approx(4.0).margin(0.15));
    REQUIRE(db(biquadMagnitude(hp, kFs, 10.0)) < -12.0);
    REQUIRE(db(biquadMagnitude(hp, kFs, 1000.0)) == Catch::Approx(0.0).margin(0.05));
}

TEST_CASE("RBJ designs hit their target gains", "[dsp][biquad]") {
    REQUIRE(db(biquadMagnitude(bq::peaking(kFs, 1000.0, 1.0, 6.0), kFs, 1000.0)) ==
            Catch::Approx(6.0).margin(1e-6));
    REQUIRE(db(biquadMagnitude(bq::peaking(kFs, 1000.0, 1.0, 6.0), kFs, 30.0)) ==
            Catch::Approx(0.0).margin(0.05));
    REQUIRE(biquadMagnitude(bq::notch(kFs, 1000.0, 4.0), kFs, 1000.0) < 1e-9);
    REQUIRE(biquadMagnitude(bq::notch(kFs, 1000.0, 4.0), kFs, 100.0) ==
            Catch::Approx(1.0).margin(0.01));
    REQUIRE(biquadMagnitude(bq::bandPass(kFs, 1000.0, 2.0), kFs, 1000.0) ==
            Catch::Approx(1.0).margin(1e-9));
    REQUIRE(db(biquadMagnitude(bq::lowShelf(kFs, 500.0, 0.7071, 9.0), kFs, 20.0)) ==
            Catch::Approx(9.0).margin(0.05));
    REQUIRE(db(biquadMagnitude(bq::lowShelf(kFs, 500.0, 0.7071, 9.0), kFs, 20000.0)) ==
            Catch::Approx(0.0).margin(0.05));
    REQUIRE(db(biquadMagnitude(bq::highShelf(kFs, 4000.0, 0.7071, -6.0), kFs, 20000.0)) ==
            Catch::Approx(-6.0).margin(0.2));
    REQUIRE(db(biquadMagnitude(bq::highShelf(kFs, 4000.0, 0.7071, -6.0), kFs, 20.0)) ==
            Catch::Approx(0.0).margin(0.05));
}

TEST_CASE("Coefficients cast once per value", "[dsp][biquad]") {
    const auto d = bq::lowPass(kFs, 1234.5);
    const auto f = d.cast<float>();
    REQUIRE(f.b0 == static_cast<float>(d.b0));
    REQUIRE(f.a2 == static_cast<float>(d.a2));
}
