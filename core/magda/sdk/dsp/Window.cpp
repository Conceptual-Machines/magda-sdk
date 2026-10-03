#include "magda/sdk/dsp/Window.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::sdk {

namespace {

// Float arithmetic on purpose: it is what juce::dsp::WindowingFunction<float> evaluates.
float ncos(std::size_t order, std::size_t i, std::size_t size) noexcept {
    return std::cos(static_cast<float>(order * i) * std::numbers::pi_v<float> /
                    static_cast<float>(size - 1));
}

double besselI0(double x) noexcept {
    const double ax = std::abs(x);
    if (ax < 3.75) {
        double y = x / 3.75;
        y *= y;
        return 1.0 +
               y * (3.5156229 +
                    y * (3.0899424 +
                         y * (1.2067492 + y * (0.2659732 + y * (0.360768e-1 + y * 0.45813e-2)))));
    }
    const double y = 3.75 / ax;
    return (std::exp(ax) / std::sqrt(ax)) *
           (0.39894228 +
            y * (0.1328592e-1 +
                 y * (0.225319e-2 +
                      y * (-0.157565e-2 +
                           y * (0.916281e-2 + y * (-0.2057706e-1 +
                                                   y * (0.2635537e-1 + y * (-0.1647633e-1 +
                                                                            y * 0.392377e-2))))))));
}

}  // namespace

void fillWindow(float* table, std::size_t size, WindowType type, bool normalise,
                float beta) noexcept {
    switch (type) {
        case WindowType::rectangular:
            for (std::size_t i = 0; i < size; ++i)
                table[i] = 1.0f;
            break;
        case WindowType::triangular: {
            const float halfSlots = 0.5f * static_cast<float>(size - 1);
            for (std::size_t i = 0; i < size; ++i)
                table[i] = 1.0f - std::abs((static_cast<float>(i) - halfSlots) / halfSlots);
            break;
        }
        case WindowType::hann:
            for (std::size_t i = 0; i < size; ++i)
                table[i] = static_cast<float>(0.5 - 0.5 * ncos(2, i, size));
            break;
        case WindowType::hamming:
            for (std::size_t i = 0; i < size; ++i)
                table[i] = static_cast<float>(0.54 - 0.46 * ncos(2, i, size));
            break;
        case WindowType::blackman: {
            constexpr float alpha = 0.16f;
            for (std::size_t i = 0; i < size; ++i)
                table[i] = static_cast<float>(0.5 * (1 - alpha) - 0.5 * ncos(2, i, size) +
                                              0.5 * alpha * ncos(4, i, size));
            break;
        }
        case WindowType::blackmanHarris:
            for (std::size_t i = 0; i < size; ++i)
                table[i] =
                    static_cast<float>(0.35875 - 0.48829 * ncos(2, i, size) +
                                       0.14128 * ncos(4, i, size) - 0.01168 * ncos(6, i, size));
            break;
        case WindowType::flatTop:
            for (std::size_t i = 0; i < size; ++i)
                table[i] =
                    static_cast<float>(1.0 - 1.93 * ncos(2, i, size) + 1.29 * ncos(4, i, size) -
                                       0.388 * ncos(6, i, size) + 0.028 * ncos(8, i, size));
            break;
        case WindowType::kaiser: {
            const double factor = 1.0 / besselI0(beta);
            const auto doubleSize = static_cast<double>(size);
            for (std::size_t i = 0; i < size; ++i) {
                const double r = (static_cast<double>(i) - 0.5 * (doubleSize - 1.0)) /
                                 (0.5 * (doubleSize - 1.0));
                table[i] =
                    static_cast<float>(besselI0(beta * std::sqrt(1.0 - std::pow(r, 2.0))) * factor);
            }
            break;
        }
    }

    if (normalise) {
        float sum = 0.0f;
        for (std::size_t i = 0; i < size; ++i)
            sum += table[i];
        const float factor = static_cast<float>(size) / sum;
        for (std::size_t i = 0; i < size; ++i)
            table[i] *= factor;
    }
}

void Window::prepare(std::size_t size, WindowType type, bool normalise, float beta) {
    table_.assign(size, 0.0f);
    fillWindow(table_.data(), size, type, normalise, beta);
}

void Window::apply(float* samples, std::size_t count) const noexcept {
    const std::size_t n = std::min(count, table_.size());
    for (std::size_t i = 0; i < n; ++i)
        samples[i] *= table_[i];
}

}  // namespace magda::sdk
