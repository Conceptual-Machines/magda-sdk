#pragma once

#include <vector>

#include "magda/sdk/dsp/Fft.hpp"
#include "magda/sdk/dsp/Window.hpp"

/**
 * @file LogMel.hpp
 * @brief Log-mel spectrogram front end for audio models (docs/measurement.md).
 */

namespace magda::sdk {

enum class MelScale {
    /// 2595 log10(1 + f / 700), as librosa and torchaudio with htk=True.
    Htk,
    /// Linear below 1 kHz and logarithmic above, librosa's default and torchaudio's "slaney".
    Slaney,
};

enum class MelSpectrum { Power, Magnitude };

/// How the signal is extended by half a frame at each end, as librosa's center=True does.
enum class MelPadding { Zero, Reflect };

enum class MelCompression {
    /// log(x + logOffset).
    Log,
    /// log1p(log1pScale * max(x, logOffset)).
    Log1p,
};

struct LogMelConfig {
    double sampleRate = 48000.0;
    /// A power of two of at least RealFft::kMinSize.
    int fftSize = 1024;
    int hopSize = 480;
    int numMels = 64;
    double fMin = 50.0;
    double fMax = 14000.0;
    MelScale scale = MelScale::Htk;
    MelSpectrum spectrum = MelSpectrum::Power;
    /// Each bin is multiplied by this before the filterbank.
    double binScale = 1.0;
    /// Scale the Hann window to a mean of one (juce::dsp::WindowingFunction's normalise).
    bool normaliseWindow = true;
    MelPadding padding = MelPadding::Zero;
    MelCompression compression = MelCompression::Log;
    double logOffset = 1e-10;
    double log1pScale = 1.0;
};

/// Triangular filters, unnormalised, as (numMels, fftSize / 2 + 1) row-major.
std::vector<float> melFilterbank(const LogMelConfig& config);

/**
 * @brief A Hann-windowed, centred STFT through a mel filterbank and a log.
 *
 * Frames start every hopSize samples with the signal padded by fftSize / 2 at each end, so
 * numSamples / hopSize + 1 frames. Output is frame-major: numFrames x numMels.
 */
class LogMelFrontEnd {
  public:
    /// Builds the window, filterbank and FFT. Allocates. False for an FFT size it cannot take.
    bool prepare(const LogMelConfig& config);

    const LogMelConfig& config() const {
        return config_;
    }
    int numFrames(int numSamples) const;

    /// Fills @p out, numFrames(numSamples) * numMels floats. Allocates for the padded signal.
    void compute(const float* samples, int numSamples, float* out);
    std::vector<float> compute(const float* samples, int numSamples);

  private:
    LogMelConfig config_;
    RealFft fft_;
    Window window_;
    std::vector<float> filterbank_;
    std::vector<float> frame_;
    std::vector<float> spectrum_;
    std::vector<float> bins_;
};

}  // namespace magda::sdk
