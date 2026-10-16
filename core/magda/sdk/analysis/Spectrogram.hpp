#pragma once

#include <vector>

#include "magda/sdk/dsp/Fft.hpp"

/**
 * @file Spectrogram.hpp
 * @brief A spectrogram for display: time by log frequency, in dB (docs/measurement.md).
 */

namespace magda::sdk {

struct SpectrogramConfig {
    double sampleRate = 48000.0;
    /// Short, so an attack is a sharp vertical edge.
    double windowSeconds = 0.0053;
    /// The window zero-padded to this, a power of two, so the low rows interpolate smoothly.
    int fftSize = 2048;
    int numRows = 128;
    double lowHz = 30.0;
    double highHz = 20000.0;
};

/**
 * @brief One column at a time: a Hann window of the signal through an FFT, then each log
 *        band's strongest bin.
 *
 * Rows run low to high. A full-scale sine reads about 0 dB in its row. Where a band is narrower
 * than one bin, the row takes the nearest bin. Allocates only in prepare().
 */
class Spectrogram {
  public:
    /// False for an FFT size RealFft cannot take, a window longer than it, or an empty band.
    bool prepare(const SpectrogramConfig& config);

    const SpectrogramConfig& config() const {
        return config_;
    }
    int numRows() const {
        return config_.numRows;
    }
    int windowLength() const {
        return static_cast<int>(window_.size());
    }

    /**
     * @brief Fills @p rowsDb (numRows() floats) for the window centred on @p centre, a sample
     *        position in @p samples; the signal is zero outside [0, numSamples).
     */
    void column(const float* samples, int numSamples, double centre, float* rowsDb) noexcept;

  private:
    SpectrogramConfig config_;
    RealFft fft_;
    std::vector<float> window_;
    float gain_ = 1.0f;
    /// numRows + 1 bin edges; row r takes bins [edges[r], max(edges[r] + 1, edges[r + 1])).
    std::vector<int> edges_;
    std::vector<float> frame_;
    std::vector<float> magnitude_;
};

}  // namespace magda::sdk
