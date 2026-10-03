#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "magda/sdk/audio/BufferView.hpp"

/**
 * @file PeakData.hpp
 * @brief Min/max waveform peaks per channel, one int16 pair per 64 samples (docs/peaks.md).
 */

namespace magda::sdk {

/// Source samples summarised by one min/max pair.
inline constexpr int kSamplesPerPeak = 64;

/// A min/max pair in the normalised [-1, 1] range.
struct PeakMinMax {
    float min = 0.0f;
    float max = 0.0f;
};

/** @brief Quantises a sample to a peak, clamping to [-1, 1] and truncating toward zero. */
inline std::int16_t floatToPeak(float v) noexcept {
    v = v < -1.0f ? -1.0f : (1.0f < v ? 1.0f : v);
    return static_cast<std::int16_t>(v * 32767.0f);
}

/** @brief The normalised value a quantised peak stands for. */
inline float peakToFloat(std::int16_t v) noexcept {
    return static_cast<float>(v) * (1.0f / 32767.0f);
}

/**
 * @brief Per-channel min/max buckets over a source of known length.
 *
 * Each bucket holds the int16 min then max of @ref kSamplesPerPeak source samples; the last may
 * cover fewer. Fill it with @ref addBlock, or rebuild stored buckets with @ref fromPacked.
 */
class PeakData {
  public:
    PeakData() = default;

    /** @brief All-silent buckets for @p numChannels channels of @p numSourceSamples samples. */
    PeakData(int numChannels, std::int64_t numSourceSamples);

    /**
     * @brief Rebuilds from stored buckets, one vector of min/max pairs per channel.
     * @return Empty if there is no channel or a vector is not exactly one pair per bucket.
     */
    static std::optional<PeakData> fromPacked(std::int64_t numSourceSamples,
                                              std::vector<std::vector<std::int16_t>> peaks);

    /**
     * @brief Writes the buckets covered by @p block, whose first frame is source sample @p
     *        firstSample.
     * @return False, writing nothing, if the channel count differs, @p firstSample is not a
     *         bucket boundary or the block runs past the source length.
     */
    bool addBlock(ConstBufferView block, std::int64_t firstSample);

    /**
     * @brief Min and max over the half-open source range [@p startSample, @p endSample).
     *
     * @p endSample is clamped to the source length and @p startSample to [0, @p endSample].
     * Returns {0, 0} for an empty range or an out-of-range channel.
     */
    PeakMinMax getMinMaxForRange(int channel, std::int64_t startSample,
                                 std::int64_t endSample) const;

    int numChannels() const noexcept {
        return static_cast<int>(peaks_.size());
    }

    std::int64_t numSourceSamples() const noexcept {
        return numSourceSamples_;
    }

    std::int64_t numBuckets() const noexcept {
        return numBuckets_;
    }

    /// Interleaved min, max pairs for @p channel, two int16 per bucket.
    std::span<const std::int16_t> channelPeaks(int channel) const {
        return peaks_[static_cast<std::size_t>(channel)];
    }

  private:
    std::int64_t numSourceSamples_ = 0;
    std::int64_t numBuckets_ = 0;
    std::vector<std::vector<std::int16_t>> peaks_;
};

}  // namespace magda::sdk
