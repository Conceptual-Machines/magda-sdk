#pragma once

#include <cstddef>
#include <vector>

/**
 * @file Window.hpp
 * @brief Windowing tables, defined as juce::dsp::WindowingFunction<float> defines them.
 */

namespace magda::sdk {

/** @brief The window shapes; the same set and order as juce::dsp::WindowingFunction. */
enum class WindowType {
    rectangular,
    triangular,
    hann,
    hamming,
    blackman,
    blackmanHarris,
    flatTop,
    kaiser,
};

/**
 * @brief Fill @p table with @p size samples of @p type.
 *
 * Symmetric windows (the denominator is size - 1). @p normalise scales the table so its mean
 * is one, which makes a DC input read at its own level. @p beta is the Kaiser shape and is
 * ignored by the rest. Allocation-free.
 */
void fillWindow(float* table, std::size_t size, WindowType type, bool normalise = true,
                float beta = 0.0f) noexcept;

/** @brief A window table held for repeated use. */
class Window {
  public:
    Window() = default;
    Window(std::size_t size, WindowType type, bool normalise = true, float beta = 0.0f) {
        prepare(size, type, normalise, beta);
    }

    /** @brief Build the table. Allocates. */
    void prepare(std::size_t size, WindowType type, bool normalise = true, float beta = 0.0f);

    std::size_t size() const noexcept {
        return table_.size();
    }

    const float* data() const noexcept {
        return table_.data();
    }

    /** @brief Multiply the first min(@p count, size()) samples of @p samples by the table. */
    void apply(float* samples, std::size_t count) const noexcept;

  private:
    std::vector<float> table_;
};

}  // namespace magda::sdk
