#include "magda/sdk/analysis/Spectrogram.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::sdk {

bool Spectrogram::prepare(const SpectrogramConfig& config) {
    config_ = config;
    const int len = static_cast<int>(std::lround(config.sampleRate * config.windowSeconds));
    if (!fft_.prepare(config.fftSize) || len < 2 || len > config.fftSize || config.numRows <= 0 ||
        config.lowHz <= 0.0 || config.highHz <= config.lowHz)
        return false;

    window_.resize(static_cast<std::size_t>(len));
    double sum = 0.0;
    for (int i = 0; i < len; ++i) {
        const double w =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (i + 0.5) / static_cast<double>(len));
        window_[static_cast<std::size_t>(i)] = static_cast<float>(w);
        sum += w;
    }
    // A sine's peak bin is amplitude * sum / 2.
    gain_ = static_cast<float>(2.0 / sum);

    const int bins = config.fftSize / 2 + 1;
    const double binHz = config.sampleRate / config.fftSize;
    edges_.resize(static_cast<std::size_t>(config.numRows + 1));
    for (int r = 0; r <= config.numRows; ++r) {
        const double hz = config.lowHz * std::pow(config.highHz / config.lowHz,
                                                  static_cast<double>(r) / config.numRows);
        edges_[static_cast<std::size_t>(r)] =
            std::clamp(static_cast<int>(std::lround(hz / binHz)), 0, bins - 1);
    }

    frame_.assign(static_cast<std::size_t>(config.fftSize), 0.0f);
    magnitude_.assign(static_cast<std::size_t>(bins), 0.0f);
    return true;
}

void Spectrogram::column(const float* samples, int numSamples, double centre,
                         float* rowsDb) noexcept {
    const int len = windowLength();
    const auto start = static_cast<long long>(std::lround(centre - 0.5 * len));
    std::fill(frame_.begin(), frame_.end(), 0.0f);
    for (int i = 0; i < len; ++i) {
        const long long s = start + i;
        if (s >= 0 && s < numSamples)
            frame_[static_cast<std::size_t>(i)] =
                samples[static_cast<std::size_t>(s)] * window_[static_cast<std::size_t>(i)];
    }
    fft_.forwardMagnitude(frame_.data(), magnitude_.data());

    const int bins = static_cast<int>(magnitude_.size());
    for (int r = 0; r < config_.numRows; ++r) {
        const int lo = edges_[static_cast<std::size_t>(r)];
        const int hi = std::min(bins, std::max(lo + 1, edges_[static_cast<std::size_t>(r + 1)]));
        float mag = 0.0f;
        for (int k = lo; k < hi; ++k)
            mag = std::max(mag, magnitude_[static_cast<std::size_t>(k)]);
        rowsDb[r] = 20.0f * std::log10(std::max(mag * gain_, 1.0e-10f));
    }
}

}  // namespace magda::sdk
