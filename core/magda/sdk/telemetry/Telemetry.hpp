#pragma once

#include <cstddef>
#include <string_view>

namespace magda::sdk {

/**
 * @brief Base for device-owned telemetry surfaces.
 *
 * A device exposes typed subclasses; the object is owned by the device and lives as long as it.
 */
class DeviceTelemetry {
  public:
    virtual ~DeviceTelemetry() = default;

    virtual std::string_view telemetryKey() const = 0;
};

/**
 * @brief A mono ring a device fills on the audio thread.
 *
 * Lock-free for one reader: @ref writePosition is the running sample count, so a reader can tell
 * whether anything arrived since it last looked.
 */
class SampleRingTelemetry : public DeviceTelemetry {
  public:
    virtual std::size_t writePosition() const = 0;
    virtual std::size_t readLatest(float* dest, int numSamples) const = 0;
};

/// An analysis tap's ring, with what a trace needs to scale it.
class AudioTapTelemetry : public SampleRingTelemetry {
  public:
    /// What the tap is fed at, which is what the frequency axis is built from.
    virtual double sampleRate() const = 0;

    virtual int traceColourIndex() const = 0;
    virtual void setTraceColourIndex(int index) = 0;
};

inline constexpr float kSilenceLufs = -100.0f;
inline constexpr float kSilenceDb = -200.0f;

/// One reading of a loudness meter.
struct LevelsSnapshot {
    float momentaryLufs = kSilenceLufs;   ///< 400 ms K-weighted window
    float shortTermLufs = kSilenceLufs;   ///< 3 s K-weighted window
    float integratedLufs = kSilenceLufs;  ///< gated, since the last reset

    float samplePeakDb = kSilenceDb;
    float truePeakDb = kSilenceDb;

    float correlation = 1.0f;  ///< L/R correlation in [-1, 1]; 1 for mono
    float width = 0.0f;        ///< side/(mid+side) energy ratio in [0, 1]

    float plr = 0.0f;  ///< peak-to-loudness ratio, LU
    float psr = 0.0f;  ///< peak-to-short-term ratio, LU

    bool plrValid = false;
    bool psrValid = false;
    bool truePeakValid = false;

    /// True once any signal has been processed.
    bool valid = false;
};

/// The loudness meter's readings, taken only while something draws them.
class LevelsTelemetry : public DeviceTelemetry {
  public:
    static constexpr std::string_view kKey = "levels";

    virtual void setActive(bool active) = 0;

    /// Restart the held figures: integrated loudness, peak holds and PLR.
    virtual void requestReset() = 0;

    virtual LevelsSnapshot snapshot() const = 0;
};

}  // namespace magda::sdk
