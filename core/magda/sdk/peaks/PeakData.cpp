#include "magda/sdk/peaks/PeakData.hpp"

#include <algorithm>
#include <limits>

namespace magda::sdk {

namespace {

std::int64_t bucketsFor(std::int64_t numSourceSamples) {
    return (numSourceSamples + kSamplesPerPeak - 1) / kSamplesPerPeak;
}

}  // namespace

PeakData::PeakData(int numChannels, std::int64_t numSourceSamples)
    : numSourceSamples_(std::max<std::int64_t>(numSourceSamples, 0)),
      numBuckets_(bucketsFor(numSourceSamples_)),
      peaks_(static_cast<std::size_t>(std::max(numChannels, 0)),
             std::vector<std::int16_t>(static_cast<std::size_t>(numBuckets_) * 2, 0)) {}

std::optional<PeakData> PeakData::fromPacked(std::int64_t numSourceSamples,
                                             std::vector<std::vector<std::int16_t>> peaks) {
    if (peaks.empty() || numSourceSamples < 0)
        return std::nullopt;
    const auto expected = static_cast<std::size_t>(bucketsFor(numSourceSamples)) * 2;
    for (const auto& channel : peaks)
        if (channel.size() != expected)
            return std::nullopt;

    PeakData data;
    data.numSourceSamples_ = numSourceSamples;
    data.numBuckets_ = bucketsFor(numSourceSamples);
    data.peaks_ = std::move(peaks);
    return data;
}

bool PeakData::addBlock(ConstBufferView block, std::int64_t firstSample) {
    const int frames = block.numFrames();
    if (block.numChannels() != numChannels() || firstSample < 0 ||
        firstSample % kSamplesPerPeak != 0 || firstSample + frames > numSourceSamples_)
        return false;

    for (int ch = 0; ch < block.numChannels(); ++ch) {
        const float* samples = block.channel(ch);
        auto& chPeaks = peaks_[static_cast<std::size_t>(ch)];

        int s = 0;
        std::int64_t b = firstSample / kSamplesPerPeak;
        while (s < frames) {
            const int bucketEnd = std::min(s + kSamplesPerPeak, frames);
            float minVal = 1.0f;
            float maxVal = -1.0f;
            for (int k = s; k < bucketEnd; ++k) {
                minVal = std::min(minVal, samples[k]);
                maxVal = std::max(maxVal, samples[k]);
            }
            // No ordered sample (all NaN) leaves min above max: silence.
            if (minVal > maxVal)
                minVal = maxVal = 0.0f;
            chPeaks[static_cast<std::size_t>(b * 2)] = floatToPeak(minVal);
            chPeaks[static_cast<std::size_t>(b * 2 + 1)] = floatToPeak(maxVal);
            ++b;
            s = bucketEnd;
        }
    }
    return true;
}

PeakMinMax PeakData::getMinMaxForRange(int channel, std::int64_t startSample,
                                       std::int64_t endSample) const {
    if (channel < 0 || channel >= numChannels() || numBuckets_ <= 0)
        return {};

    endSample = std::clamp<std::int64_t>(endSample, 0, numSourceSamples_);
    startSample = std::clamp<std::int64_t>(startSample, 0, endSample);
    if (startSample >= endSample)
        return {};

    const std::int64_t firstBucket = startSample / kSamplesPerPeak;
    const std::int64_t lastBucket = std::min(numBuckets_ - 1, (endSample - 1) / kSamplesPerPeak);
    const auto& chPeaks = peaks_[static_cast<std::size_t>(channel)];

    std::int16_t minI = std::numeric_limits<std::int16_t>::max();
    std::int16_t maxI = std::numeric_limits<std::int16_t>::min();
    for (std::int64_t b = firstBucket; b <= lastBucket; ++b) {
        minI = std::min(minI, chPeaks[static_cast<std::size_t>(b * 2)]);
        maxI = std::max(maxI, chPeaks[static_cast<std::size_t>(b * 2 + 1)]);
    }
    if (minI > maxI)
        return {};
    return {peakToFloat(minI), peakToFloat(maxI)};
}

}  // namespace magda::sdk
