#pragma once

#include <cmath>
#include <complex>
#include <numbers>

/**
 * @file Biquad.hpp
 * @brief One transposed direct form II biquad, and coefficient design for the common types.
 */

namespace magda::sdk {

/** @brief Normalised coefficients: feed-forward b0..b2, feedback a1, a2 (a0 divided out). */
template <typename T>
struct BiquadCoeffs {
    T b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    /** @brief The same filter at another precision, each coefficient rounded once. */
    template <typename U>
    BiquadCoeffs<U> cast() const noexcept {
        return {static_cast<U>(b0), static_cast<U>(b1), static_cast<U>(b2), static_cast<U>(a1),
                static_cast<U>(a2)};
    }
};

/** @brief The two delay registers of one channel. */
template <typename T>
struct BiquadState {
    T z1 = 0, z2 = 0;

    void reset() noexcept {
        z1 = 0;
        z2 = 0;
    }
};

/** @brief One sample through @p c with the registers in @p s. Audio-thread safe. */
template <typename T>
inline T processBiquad(const BiquadCoeffs<T>& c, BiquadState<T>& s, T x) noexcept {
    const T y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

/** @brief Coefficients and registers together, for one channel with its own filter. */
template <typename T>
class Biquad {
  public:
    Biquad() = default;
    explicit Biquad(const BiquadCoeffs<T>& coeffs) noexcept : coeffs_(coeffs) {}

    /** @brief Swap the coefficients; the registers are kept. */
    void setCoefficients(const BiquadCoeffs<T>& coeffs) noexcept {
        coeffs_ = coeffs;
    }

    const BiquadCoeffs<T>& coefficients() const noexcept {
        return coeffs_;
    }

    void reset() noexcept {
        state_.reset();
    }

    T process(T x) noexcept {
        return processBiquad(coeffs_, state_, x);
    }

  private:
    BiquadCoeffs<T> coeffs_;
    BiquadState<T> state_;
};

/**
 * @brief Coefficient design, always in double.
 *
 * lowPass and highPass reproduce juce::IIRCoefficients bit for bit once cast to float. The
 * K-suffixed forms are the BS.1770 / libebur128 design, which divides by a0 last.
 */
namespace biquad {

/** @brief Second-order low-pass at @p frequency (JUCE's coefficient order of operations). */
inline BiquadCoeffs<double> lowPass(double sampleRate, double frequency,
                                    double q = 1.0 / std::numbers::sqrt2) noexcept {
    const auto n = 1.0 / std::tan(std::numbers::pi * frequency / sampleRate);
    const auto nSquared = n * n;
    const auto c1 = 1.0 / (1.0 + (n / q) + nSquared);
    return {c1, c1 * 2.0, c1, c1 * 2.0 * (1.0 - nSquared), c1 * (1.0 - (n / q) + nSquared)};
}

/** @brief Second-order high-pass at @p frequency (JUCE's coefficient order of operations). */
inline BiquadCoeffs<double> highPass(double sampleRate, double frequency,
                                     double q = 1.0 / std::numbers::sqrt2) noexcept {
    const auto n = std::tan(std::numbers::pi * frequency / sampleRate);
    const auto nSquared = n * n;
    const auto c1 = 1.0 / (1.0 + (n / q) + nSquared);
    return {c1, c1 * -2.0, c1, c1 * 2.0 * (nSquared - 1.0), c1 * (1.0 - (n / q) + nSquared)};
}

/** @brief BS.1770 high-pass: K = tan(pi f / fs), everything divided by a0 last. */
inline BiquadCoeffs<double> highPassK(double sampleRate, double frequency, double q) noexcept {
    const double K = std::tan(std::numbers::pi * frequency / sampleRate);
    const double a0 = 1.0 + K / q + K * K;
    return {1.0 / a0, -2.0 / a0, 1.0 / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / q + K * K) / a0};
}

/**
 * @brief BS.1770 high shelf with boost @p vh and band gain @p vb (both linear).
 */
inline BiquadCoeffs<double> highShelfK(double sampleRate, double frequency, double vh, double vb,
                                       double q) noexcept {
    const double K = std::tan(std::numbers::pi * frequency / sampleRate);
    const double a0 = 1.0 + K / q + K * K;
    return {(vh + vb * K / q + K * K) / a0, 2.0 * (K * K - vh) / a0,
            (vh - vb * K / q + K * K) / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / q + K * K) / a0};
}

namespace detail {

struct Rbj {
    double cosW, alpha;
};

inline Rbj rbj(double sampleRate, double frequency, double q) noexcept {
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    return {std::cos(w), std::sin(w) / (2.0 * q)};
}

inline BiquadCoeffs<double> over(double b0, double b1, double b2, double a0, double a1,
                                 double a2) noexcept {
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

}  // namespace detail

/** @brief Band-pass with 0 dB at the centre (RBJ constant-peak-gain form). */
inline BiquadCoeffs<double> bandPass(double sampleRate, double frequency, double q) noexcept {
    const auto [c, a] = detail::rbj(sampleRate, frequency, q);
    return detail::over(a, 0.0, -a, 1.0 + a, -2.0 * c, 1.0 - a);
}

/** @brief Notch at @p frequency. */
inline BiquadCoeffs<double> notch(double sampleRate, double frequency, double q) noexcept {
    const auto [c, a] = detail::rbj(sampleRate, frequency, q);
    return detail::over(1.0, -2.0 * c, 1.0, 1.0 + a, -2.0 * c, 1.0 - a);
}

/** @brief Peaking EQ of @p gainDb at @p frequency (RBJ). */
inline BiquadCoeffs<double> peaking(double sampleRate, double frequency, double q,
                                    double gainDb) noexcept {
    const auto [c, a] = detail::rbj(sampleRate, frequency, q);
    const double A = std::pow(10.0, gainDb / 40.0);
    return detail::over(1.0 + a * A, -2.0 * c, 1.0 - a * A, 1.0 + a / A, -2.0 * c, 1.0 - a / A);
}

/** @brief Low shelf of @p gainDb below @p frequency (RBJ). */
inline BiquadCoeffs<double> lowShelf(double sampleRate, double frequency, double q,
                                     double gainDb) noexcept {
    const auto [c, a] = detail::rbj(sampleRate, frequency, q);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double t = 2.0 * std::sqrt(A) * a;
    return detail::over(A * ((A + 1.0) - (A - 1.0) * c + t), 2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                        A * ((A + 1.0) - (A - 1.0) * c - t), (A + 1.0) + (A - 1.0) * c + t,
                        -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - t);
}

/** @brief High shelf of @p gainDb above @p frequency (RBJ). */
inline BiquadCoeffs<double> highShelf(double sampleRate, double frequency, double q,
                                      double gainDb) noexcept {
    const auto [c, a] = detail::rbj(sampleRate, frequency, q);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double t = 2.0 * std::sqrt(A) * a;
    return detail::over(A * ((A + 1.0) + (A - 1.0) * c + t), -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                        A * ((A + 1.0) + (A - 1.0) * c - t), (A + 1.0) - (A - 1.0) * c + t,
                        2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - t);
}

}  // namespace biquad

/** @brief Linear gain of @p c at @p frequency. Not for the audio thread. */
inline double biquadMagnitude(const BiquadCoeffs<double>& c, double sampleRate,
                              double frequency) noexcept {
    const auto z1 = std::polar(1.0, -2.0 * std::numbers::pi * frequency / sampleRate);
    const auto z2 = z1 * z1;
    return std::abs((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

}  // namespace magda::sdk
