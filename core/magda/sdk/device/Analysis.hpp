#pragma once

#include <span>
#include <string>
#include <utility>

namespace magda::sdk {

/// What an analysis produced: the device's JSON, or why it failed.
struct AnalysisResult {
    bool ok = false;
    std::string json;
    std::string message;

    static AnalysisResult success(std::string json) {
        return {true, std::move(json), {}};
    }
    static AnalysisResult error(std::string message) {
        return {false, {}, std::move(message)};
    }
};

/**
 * @brief Optional beside Device: a device that turns recorded audio into something of its own,
 * such as a patch resynthesised from a sample.
 *
 * Control thread, never concurrent with process(); it may allocate.
 */
class AnalyzingDevice {
  public:
    virtual ~AnalyzingDevice() = default;

    virtual AnalysisResult analyze(std::span<const float> mono, double sampleRate) = 0;
};

}  // namespace magda::sdk
