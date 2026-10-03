#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace magda::sdk {

/**
 * @brief Highest absolute sample in a block, ignoring NaN; an infinity counts.
 *
 * Eight independent lanes, so the compare-and-select vectorises without fast-math. A maximum
 * is exact in any order, so the lane split cannot change the result.
 */
inline float peakMagnitude(const float* samples, int numSamples) noexcept {
    if (samples == nullptr || numSamples <= 0)
        return 0.0f;

    constexpr int kLanes = 8;
    std::array<float, kLanes> lanes{};
    const int vectorEnd = numSamples - numSamples % kLanes;

    for (int i = 0; i < vectorEnd; i += kLanes) {
        for (int lane = 0; lane < kLanes; ++lane) {
            const float magnitude = std::abs(samples[i + lane]);
            auto& peak = lanes[static_cast<std::size_t>(lane)];
            peak = magnitude > peak ? magnitude : peak;
        }
    }

    float peak = 0.0f;
    for (const float lanePeak : lanes)
        peak = lanePeak > peak ? lanePeak : peak;
    for (int i = vectorEnd; i < numSamples; ++i) {
        const float magnitude = std::abs(samples[i]);
        peak = magnitude > peak ? magnitude : peak;
    }
    return peak;
}

}  // namespace magda::sdk
