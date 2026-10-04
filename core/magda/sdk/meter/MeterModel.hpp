#pragma once

#include <array>
#include <span>

#include "magda/sdk/tap/LevelTap.hpp"

/**
 * @file MeterModel.hpp
 * @brief Level meter state: ballistics, peak hold and clip latch (docs/meter.md).
 */

namespace magda::sdk {

/// Maps dB to a 0..1 meter position along a power curve.
struct MeterScale {
    float minDb = -60.0f;
    float maxDb = 6.0f;
    float curveExponent = 3.0f;

    /// minDb for silence.
    float gainToDb(float gain) const;
    float dbToPosition(float db) const;
    float positionToDb(float position) const;
};

/// Coefficients are per 60 Hz frame and rescaled to the elapsed time.
struct MeterBallistics {
    static constexpr float kNominalFrameMs = 1000.0f / 60.0f;

    float attackAt60Hz = 0.9f;
    float releaseAt60Hz = 0.05f;
    float peakHoldMs = 1500.0f;
    float peakDecayDbPerMs = 0.8f / kNominalFrameMs;
    /// A displayed gain below this snaps to silence.
    float silenceGain = 0.001f;
    /// A target above this latches the clip flag.
    float clipGain = 1.0f;
    /// Targets are clamped to this, so a runaway signal still decays in finite time.
    float maxGain = 2.0f;

    /// Moves @p displayGain toward @p targetGain. @return true when it moved visibly.
    bool follow(float& displayGain, float targetGain, float elapsedMs) const;
};

/**
 * @brief Pure meter state, advanced by elapsed time rather than a clock.
 *
 * setTargets() takes each reading as it arrives (a LevelTap read), advance() runs the ballistics
 * once per frame. Up to two channels, as LevelTap publishes.
 */
class MeterModel {
  public:
    static constexpr int kMaxChannels = engine::LevelTap::kNumChannels;

    struct Channel {
        float targetGain = 0.0f;
        float displayGain = 0.0f;
        float peakDb = 0.0f;
        float holdMs = 0.0f;
        bool clipped = false;
    };

    explicit MeterModel(int numChannels = kMaxChannels, MeterBallistics ballistics = {},
                        MeterScale scale = {});

    /// One gain per channel; a missing channel reads as the first. Raises the peaks at once.
    void setTargets(std::span<const float> gains);
    void setTargets(const engine::LevelTap::Levels& levels);

    /// @return true when anything a painter draws moved.
    bool advance(float elapsedMs);

    /// Silent display and peaks at the floor: a host can stop advancing.
    bool isIdle() const;

    /// Drops peak holds and clip latches.
    void resetPeaks();
    void clearClips();

    int numChannels() const {
        return numChannels_;
    }
    const Channel& channel(int index) const {
        return channels_[static_cast<std::size_t>(index)];
    }
    float loudestDisplayGain() const;
    float loudestPeakDb() const;
    bool anyClipped() const;

    const MeterBallistics& ballistics() const {
        return ballistics_;
    }
    const MeterScale& scale() const {
        return scale_;
    }

  private:
    int numChannels_;
    MeterBallistics ballistics_;
    MeterScale scale_;
    std::array<Channel, kMaxChannels> channels_{};
};

}  // namespace magda::sdk
