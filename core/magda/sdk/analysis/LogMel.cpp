#include "magda/sdk/analysis/LogMel.hpp"

#include <algorithm>
#include <cmath>

namespace magda::sdk {

namespace {

constexpr double kSlaneyLinearStep = 200.0 / 3.0;
constexpr double kSlaneyLogStartHz = 1000.0;
const double kSlaneyLogStep = std::log(6.4) / 27.0;

double hzToMel(double hz, MelScale scale) {
    if (scale == MelScale::Htk)
        return 2595.0 * std::log10(1.0 + hz / 700.0);
    const double logStartMel = kSlaneyLogStartHz / kSlaneyLinearStep;
    if (hz < kSlaneyLogStartHz)
        return hz / kSlaneyLinearStep;
    return logStartMel + std::log(hz / kSlaneyLogStartHz) / kSlaneyLogStep;
}

double melToHz(double mel, MelScale scale) {
    if (scale == MelScale::Htk)
        return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
    const double logStartMel = kSlaneyLogStartHz / kSlaneyLinearStep;
    if (mel < logStartMel)
        return mel * kSlaneyLinearStep;
    return kSlaneyLogStartHz * std::exp((mel - logStartMel) * kSlaneyLogStep);
}

}  // namespace

std::vector<float> melFilterbank(const LogMelConfig& config) {
    const int bins = config.fftSize / 2 + 1;
    const int mels = std::max(0, config.numMels);
    std::vector<float> filterbank(static_cast<std::size_t>(mels) * static_cast<std::size_t>(bins),
                                  0.0f);

    // numMels + 2 edges evenly spaced in mels; filter m rises from edge m to m + 1 and falls to
    // m + 2.
    std::vector<double> edges(static_cast<std::size_t>(mels) + 2);
    const double melMin = hzToMel(config.fMin, config.scale);
    const double melMax = hzToMel(config.fMax, config.scale);
    for (std::size_t i = 0; i < edges.size(); ++i)
        edges[i] =
            melToHz(melMin + (melMax - melMin) * static_cast<double>(i) / (mels + 1), config.scale);

    for (int m = 0; m < mels; ++m) {
        const double left = edges[static_cast<std::size_t>(m)];
        const double centre = edges[static_cast<std::size_t>(m) + 1];
        const double right = edges[static_cast<std::size_t>(m) + 2];
        for (int bin = 0; bin < bins; ++bin) {
            const double hz = static_cast<double>(bin) * config.sampleRate / config.fftSize;
            double weight = 0.0;
            if (hz >= left && hz <= centre && centre > left)
                weight = (hz - left) / (centre - left);
            else if (hz > centre && hz <= right && right > centre)
                weight = (right - hz) / (right - centre);
            filterbank[static_cast<std::size_t>(m) * static_cast<std::size_t>(bins) +
                       static_cast<std::size_t>(bin)] = static_cast<float>(weight);
        }
    }
    return filterbank;
}

bool LogMelFrontEnd::prepare(const LogMelConfig& config) {
    config_ = config;
    if (!fft_.prepare(config.fftSize) || config.hopSize <= 0 || config.numMels <= 0)
        return false;
    window_.prepare(static_cast<std::size_t>(config.fftSize), WindowType::hann,
                    config.normaliseWindow);
    filterbank_ = melFilterbank(config);
    frame_.assign(static_cast<std::size_t>(config.fftSize), 0.0f);
    spectrum_.assign(static_cast<std::size_t>(fft_.spectrumValues()), 0.0f);
    bins_.assign(static_cast<std::size_t>(config.fftSize / 2 + 1), 0.0f);
    return true;
}

int LogMelFrontEnd::numFrames(int numSamples) const {
    const int pad = config_.fftSize / 2;
    // Reflection needs more signal than it mirrors.
    if (numSamples <= 0 || (config_.padding == MelPadding::Reflect && numSamples <= 2 * pad))
        return 0;
    return (numSamples + 2 * pad - config_.fftSize) / config_.hopSize + 1;
}

void LogMelFrontEnd::compute(const float* samples, int numSamples, float* out) {
    const int frames = numFrames(numSamples);
    if (frames <= 0)
        return;

    const auto n = static_cast<std::size_t>(numSamples);
    const auto pad = static_cast<std::size_t>(config_.fftSize / 2);
    std::vector<float> padded(n + 2 * pad, 0.0f);
    std::copy_n(samples, n, padded.begin() + static_cast<std::ptrdiff_t>(pad));
    if (config_.padding == MelPadding::Reflect) {
        for (std::size_t i = 1; i <= pad; ++i) {
            padded[pad - i] = samples[i];
            padded[pad + n - 1 + i] = samples[n - 1 - i];
        }
    }

    const auto fftSize = static_cast<std::size_t>(config_.fftSize);
    const std::size_t bins = bins_.size();
    const auto mels = static_cast<std::size_t>(config_.numMels);

    for (int f = 0; f < frames; ++f) {
        std::copy_n(padded.begin() + static_cast<std::ptrdiff_t>(f) * config_.hopSize, fftSize,
                    frame_.begin());
        window_.apply(frame_.data(), fftSize);

        if (config_.spectrum == MelSpectrum::Magnitude) {
            fft_.forwardMagnitude(frame_.data(), bins_.data());
        } else {
            fft_.forward(frame_.data(), spectrum_.data());
            for (std::size_t k = 0; k < bins; ++k) {
                const float re = spectrum_[2 * k];
                const float im = spectrum_[2 * k + 1];
                bins_[k] = re * re + im * im;
            }
        }

        for (std::size_t m = 0; m < mels; ++m) {
            const float* row = filterbank_.data() + m * bins;
            double energy = 0.0;
            for (std::size_t k = 0; k < bins; ++k)
                energy += static_cast<double>(bins_[k]) * config_.binScale * row[k];
            const double value =
                config_.compression == MelCompression::Log
                    ? std::log(energy + config_.logOffset)
                    : std::log1p(config_.log1pScale * std::max(energy, config_.logOffset));
            out[static_cast<std::size_t>(f) * mels + m] = static_cast<float>(value);
        }
    }
}

std::vector<float> LogMelFrontEnd::compute(const float* samples, int numSamples) {
    std::vector<float> out(static_cast<std::size_t>(std::max(0, numFrames(numSamples))) *
                           static_cast<std::size_t>(std::max(0, config_.numMels)));
    compute(samples, numSamples, out.data());
    return out;
}

}  // namespace magda::sdk
