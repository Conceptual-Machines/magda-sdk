#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

/**
 * @file Oversampler.hpp
 * @brief Polyphase 2x/4x up and down samplers on one Hann-windowed sinc kernel.
 *
 * The kernel is factor * tapsPerPhase long with its cutoff at the base-rate Nyquist. Preparing
 * allocates; processing does not.
 */

namespace magda::sdk {

/** @brief Taps per polyphase branch that gives a 48-tap kernel at 2x and 4x. */
inline int defaultTapsPerPhase(int factor) noexcept {
    return 48 / factor;
}

namespace detail {

/** @brief The Hann-windowed sinc, cutoff at base-rate Nyquist, before any gain is applied. */
inline std::vector<double> oversamplerKernel(int factor, int tapsPerPhase) {
    const int total = factor * tapsPerPhase;
    const double fc = 0.5 / factor;
    std::vector<double> kernel(static_cast<std::size_t>(total));
    const double mid = (total - 1) / 2.0;
    for (int n = 0; n < total; ++n) {
        const double x = n - mid;
        const double sinc = std::abs(x) < 1.0e-9 ? 2.0 * fc
                                                 : std::sin(2.0 * std::numbers::pi * fc * x) /
                                                       (std::numbers::pi * x);
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / (total - 1));
        kernel[static_cast<std::size_t>(n)] = sinc * w;
    }
    return kernel;
}

inline double kernelSum(const std::vector<double>& kernel) noexcept {
    double sum = 0.0;
    for (double v : kernel)
        sum += v;
    return sum;
}

/** @brief Newest-first history over a buffer holding two copies, so a window is contiguous. */
class History {
  public:
    void prepare(int length) {
        length_ = length;
        samples_.assign(static_cast<std::size_t>(2 * length), 0.0f);
        newest_ = 0;
    }

    void clear() noexcept {
        std::fill(samples_.begin(), samples_.end(), 0.0f);
        newest_ = 0;
    }

    void push(float x) noexcept {
        newest_ = (newest_ == 0 ? length_ : newest_) - 1;
        samples_[static_cast<std::size_t>(newest_)] = x;
        samples_[static_cast<std::size_t>(newest_ + length_)] = x;
    }

    const float* window() const noexcept {
        return samples_.data() + newest_;
    }

  private:
    std::vector<float> samples_;
    int length_ = 0;
    int newest_ = 0;
};

}  // namespace detail

/** @brief Zero-stuffing upsampler as @p factor polyphase branches, unity passband gain. */
class PolyphaseUpsampler {
  public:
    /** @brief Design the kernel for @p factor (2 or 4) and clear the history. Allocates. */
    void prepare(int factor, int tapsPerPhase) {
        factor_ = factor;
        taps_ = tapsPerPhase;
        const auto kernel = detail::oversamplerKernel(factor, tapsPerPhase);
        const double sum = detail::kernelSum(kernel);
        const double norm = sum > 1.0e-12 ? (factor / sum) : 1.0;
        coeffs_.assign(static_cast<std::size_t>(factor * tapsPerPhase), 0.0f);
        for (int phase = 0; phase < factor; ++phase)
            for (int t = 0; t < tapsPerPhase; ++t)
                coeffs_[static_cast<std::size_t>(phase * tapsPerPhase + t)] =
                    static_cast<float>(kernel[static_cast<std::size_t>(t * factor + phase)] * norm);
        history_.prepare(tapsPerPhase);
    }

    void reset() noexcept {
        history_.clear();
    }

    int factor() const noexcept {
        return factor_;
    }

    int tapsPerPhase() const noexcept {
        return taps_;
    }

    /** @brief Push one input sample and hand @p emit its @c factor() outputs, in order. */
    template <typename Emit>
    void processSample(float x, Emit&& emit) noexcept {
        history_.push(x);
        const float* window = history_.window();
        for (int phase = 0; phase < factor_; ++phase) {
            float acc = 0.0f;
            const float* c = coeffs_.data() + static_cast<std::size_t>(phase * taps_);
            for (int t = 0; t < taps_; ++t)
                acc += c[t] * window[t];
            emit(acc);
        }
    }

    /** @brief @p numSamples in, @p numSamples * factor() out. */
    void process(const float* in, int numSamples, float* out) noexcept {
        for (int i = 0; i < numSamples; ++i)
            processSample(in[i], [&out](float y) { *out++ = y; });
    }

  private:
    int factor_ = 0;
    int taps_ = 0;
    std::vector<float> coeffs_;
    detail::History history_;
};

/** @brief FIR decimator on the same kernel, unity gain at DC. */
class PolyphaseDownsampler {
  public:
    /** @brief Design the kernel for @p factor (2 or 4) and clear the history. Allocates. */
    void prepare(int factor, int tapsPerPhase) {
        factor_ = factor;
        const auto kernel = detail::oversamplerKernel(factor, tapsPerPhase);
        const double sum = detail::kernelSum(kernel);
        const double norm = sum > 1.0e-12 ? 1.0 / sum : 1.0;
        coeffs_.resize(kernel.size());
        for (std::size_t i = 0; i < kernel.size(); ++i)
            coeffs_[i] = static_cast<float>(kernel[i] * norm);
        history_.prepare(factor * tapsPerPhase);
    }

    void reset() noexcept {
        history_.clear();
    }

    int factor() const noexcept {
        return factor_;
    }

    /** @brief @p numOut * factor() samples in, @p numOut out. */
    void process(const float* in, int numOut, float* out) noexcept {
        const int taps = static_cast<int>(coeffs_.size());
        for (int i = 0; i < numOut; ++i) {
            for (int k = 0; k < factor_; ++k)
                history_.push(*in++);
            const float* window = history_.window();
            float acc = 0.0f;
            for (int t = 0; t < taps; ++t)
                acc += coeffs_[static_cast<std::size_t>(t)] * window[t];
            out[i] = acc;
        }
    }

  private:
    int factor_ = 0;
    std::vector<float> coeffs_;
    detail::History history_;
};

/** @brief An up and down pair for one channel, with the round-trip latency it adds. */
class Oversampler {
  public:
    /** @brief @p factor is 2 or 4. @p tapsPerPhase <= 0 takes defaultTapsPerPhase(). Allocates. */
    void prepare(int factor, int tapsPerPhase = 0) {
        factor_ = factor;
        const int taps = tapsPerPhase > 0 ? tapsPerPhase : defaultTapsPerPhase(factor);
        latency_ = taps - 1;
        up_.prepare(factor, taps);
        down_.prepare(factor, taps);
    }

    void reset() noexcept {
        up_.reset();
        down_.reset();
    }

    int factor() const noexcept {
        return factor_;
    }

    /** @brief Round-trip delay in base-rate samples: tapsPerPhase - 1. */
    int latencySamples() const noexcept {
        return latency_;
    }

    PolyphaseUpsampler& up() noexcept {
        return up_;
    }

    PolyphaseDownsampler& down() noexcept {
        return down_;
    }

    /** @brief @p numSamples in, @p numSamples * factor() out. */
    void upsample(const float* in, int numSamples, float* out) noexcept {
        up_.process(in, numSamples, out);
    }

    /** @brief @p numOut * factor() in, @p numOut out. */
    void downsample(const float* in, int numOut, float* out) noexcept {
        down_.process(in, numOut, out);
    }

  private:
    int factor_ = 0;
    int latency_ = 0;
    PolyphaseUpsampler up_;
    PolyphaseDownsampler down_;
};

}  // namespace magda::sdk
