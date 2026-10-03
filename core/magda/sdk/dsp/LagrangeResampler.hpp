#pragma once

#include <algorithm>

/**
 * @file LagrangeResampler.hpp
 * @brief Five-point Lagrange resampler of one stream, the algorithm of juce::LagrangeInterpolator.
 */

namespace magda::sdk {

/**
 * @brief Resamples one stream by a speed ratio, keeping five samples of history between calls.
 *
 * Audio-thread safe: no allocation and no locks. Call reset() on a break in the input.
 */
class LagrangeResampler {
  public:
    LagrangeResampler() noexcept {
        reset();
    }

    /** @brief Clear the history; the first output starts from silence. */
    void reset() noexcept {
        index_ = 0;
        subSamplePos_ = 1.0;
        std::fill(std::begin(history_), std::end(history_), 0.0f);
    }

    /**
     * @brief Produce @p numOutput samples, consuming @p speedRatio input samples for each.
     * @param speedRatio Input samples per output sample (source rate over target rate).
     * @return The input samples used; @p input must hold at least that many.
     */
    int process(double speedRatio, const float* input, float* output, int numOutput) noexcept {
        int used = 0;
        double pos = subSamplePos_;
        for (int i = 0; i < numOutput; ++i) {
            while (pos >= 1.0) {
                push(input[used++]);
                pos -= 1.0;
            }
            output[i] = valueAtOffset(static_cast<float>(pos));
            pos += speedRatio;
        }
        subSamplePos_ = pos;
        return used;
    }

  private:
    static constexpr int kMemory = 5;

    void push(float value) noexcept {
        history_[index_] = value;
        if (++index_ == kMemory)
            index_ = 0;
    }

    // Float arithmetic in the order juce::Interpolators::LagrangeTraits evaluates it.
    float valueAtOffset(float offset) const noexcept {
        float result = 0.0f;
        int at = index_;
        for (int k = 0; k < kMemory; ++k) {
            float a = history_[at];
            for (int j = 0; j < kMemory; ++j) {
                const int d = j - k;
                if (d != 0)
                    a *= (static_cast<float>(j - 2) - offset) * (1.0f / static_cast<float>(d));
            }
            result += a;
            if (++at == kMemory)
                at = 0;
        }
        return result;
    }

    float history_[kMemory];
    double subSamplePos_ = 1.0;
    int index_ = 0;
};

}  // namespace magda::sdk
