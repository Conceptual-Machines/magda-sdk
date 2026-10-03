#pragma once

#include <cstdint>
#include <functional>
#include <magda/sdk/audio/BufferView.hpp>
#include <vector>

/**
 * @file TransientDetector.hpp
 * @brief Where a source's beats are: onset positions from one channel of audio.
 *
 * Offline and two-pass: the threshold is relative to the source's own peak, so the peak is
 * found before the first sample is judged. Time domain only: two envelope followers, a
 * differentiator, a third follower, and a threshold with a retrigger lockout. A different
 * detector would move every auto-detected marker in every saved project.
 */

namespace magda::sdk {

struct TransientDetectionSettings {
    /// 0 finds few, 1 finds many; the threshold is `-10 - sensitivity * 30` dB below the peak.
    float sensitivity = 0.5f;

    /// Transients closer together than this are thinned until none are.
    double minimumSpacingSeconds = 0.1;

    /// How long after a trigger the detector will not fire again.
    double retriggerSeconds = 0.05;

    bool operator==(const TransientDetectionSettings&) const = default;
};

/**
 * @brief Fills @p destination with @p count frames of the first channel from frame @p start.
 *
 * Returns the frames available, which is short only at the end of the source, and clears the
 * rest of @p destination.
 */
using TransientBlockReader = std::function<int(float* destination, std::int64_t start, int count)>;

/**
 * @brief Transient positions in a source read through @p read, in seconds from its start.
 *
 * Ascending and never closer than TransientDetectionSettings::minimumSpacingSeconds. Reads the
 * source in order twice, in blocks of 32768 frames, so a long file needs no more memory than
 * one block. Empty for no frames, no rate, or nothing above the threshold.
 */
std::vector<double> detectTransients(const TransientBlockReader& read, std::int64_t totalFrames,
                                     double sampleRate, const TransientDetectionSettings& settings);

/** @brief The same over audio in memory; only the first channel is analysed. */
std::vector<double> detectTransients(ConstBufferView audio, double sampleRate,
                                     const TransientDetectionSettings& settings);

}  // namespace magda::sdk
