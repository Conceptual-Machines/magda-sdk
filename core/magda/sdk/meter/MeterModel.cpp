#include "magda/sdk/meter/MeterModel.hpp"

#include <algorithm>
#include <cmath>

namespace magda::sdk {

float MeterScale::gainToDb(float gain) const {
    if (gain <= 0.0f)
        return minDb;
    return 20.0f * std::log10(gain);
}

float MeterScale::dbToPosition(float db) const {
    if (db <= minDb)
        return 0.0f;
    if (db >= maxDb)
        return 1.0f;
    return std::pow((db - minDb) / (maxDb - minDb), curveExponent);
}

float MeterScale::positionToDb(float position) const {
    if (position <= 0.0f)
        return minDb;
    if (position >= 1.0f)
        return maxDb;
    return minDb + std::pow(position, 1.0f / curveExponent) * (maxDb - minDb);
}

bool MeterBallistics::follow(float& displayGain, float targetGain, float elapsedMs) const {
    const float previous = displayGain;
    const float coefficientAt60Hz = targetGain > displayGain ? attackAt60Hz : releaseAt60Hz;
    const float coefficient =
        1.0f - std::pow(1.0f - coefficientAt60Hz, std::max(0.0f, elapsedMs) / kNominalFrameMs);
    displayGain += (targetGain - displayGain) * coefficient;
    if (displayGain < silenceGain)
        displayGain = 0.0f;
    return std::abs(displayGain - previous) > 0.0001f;
}

MeterModel::MeterModel(int numChannels, MeterBallistics ballistics, MeterScale scale)
    : numChannels_(std::clamp(numChannels, 1, kMaxChannels)),
      ballistics_(ballistics),
      scale_(scale) {
    resetPeaks();
}

void MeterModel::setTargets(std::span<const float> gains) {
    if (gains.empty())
        return;

    for (int i = 0; i < numChannels_; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const float raw = index < gains.size() ? gains[index] : gains[0];
        auto& channel = channels_[index];
        channel.targetGain = std::clamp(raw, 0.0f, ballistics_.maxGain);
        if (raw > ballistics_.clipGain)
            channel.clipped = true;

        const float db = scale_.gainToDb(channel.targetGain);
        if (db > channel.peakDb) {
            channel.peakDb = db;
            channel.holdMs = ballistics_.peakHoldMs;
        }
    }
}

void MeterModel::setTargets(const engine::LevelTap::Levels& levels) {
    setTargets(std::span<const float>(levels.peak));
}

bool MeterModel::advance(float elapsedMs) {
    elapsedMs = std::max(0.0f, elapsedMs);
    bool changed = false;

    for (int i = 0; i < numChannels_; ++i) {
        auto& channel = channels_[static_cast<std::size_t>(i)];

        changed |= ballistics_.follow(channel.displayGain, channel.targetGain, elapsedMs);

        const float previousPeak = channel.peakDb;
        const float targetDb = scale_.gainToDb(channel.targetGain);
        if (targetDb > channel.peakDb) {
            channel.peakDb = targetDb;
            channel.holdMs = ballistics_.peakHoldMs;
        } else if (channel.holdMs > 0.0f) {
            channel.holdMs = std::max(0.0f, channel.holdMs - elapsedMs);
        } else {
            channel.peakDb =
                std::max(scale_.minDb, channel.peakDb - ballistics_.peakDecayDbPerMs * elapsedMs);
        }
        changed |= std::abs(channel.peakDb - previousPeak) > 0.01f;
    }
    return changed;
}

bool MeterModel::isIdle() const {
    for (int i = 0; i < numChannels_; ++i) {
        const auto& channel = channels_[static_cast<std::size_t>(i)];
        if (channel.displayGain >= ballistics_.silenceGain || channel.peakDb > scale_.minDb)
            return false;
    }
    return true;
}

void MeterModel::resetPeaks() {
    for (auto& channel : channels_) {
        channel.peakDb = scale_.minDb;
        channel.holdMs = 0.0f;
        channel.clipped = false;
    }
}

void MeterModel::clearClips() {
    for (auto& channel : channels_)
        channel.clipped = false;
}

float MeterModel::loudestDisplayGain() const {
    float loudest = 0.0f;
    for (int i = 0; i < numChannels_; ++i)
        loudest = std::max(loudest, channels_[static_cast<std::size_t>(i)].displayGain);
    return loudest;
}

float MeterModel::loudestPeakDb() const {
    float loudest = scale_.minDb;
    for (int i = 0; i < numChannels_; ++i)
        loudest = std::max(loudest, channels_[static_cast<std::size_t>(i)].peakDb);
    return loudest;
}

bool MeterModel::anyClipped() const {
    for (int i = 0; i < numChannels_; ++i)
        if (channels_[static_cast<std::size_t>(i)].clipped)
            return true;
    return false;
}

}  // namespace magda::sdk
