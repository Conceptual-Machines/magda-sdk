#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <magda/sdk/audio/BlockPeak.hpp>
#include <magda/sdk/audio/BufferView.hpp>
#include <magda/sdk/dsp/Biquad.hpp>
#include <magda/sdk/dsp/Oversampler.hpp>
#include <magda/sdk/tap/SampleRing.hpp>
#include <magda/sdk/telemetry/Telemetry.hpp>

/**
 * @file TrackMeasurer.hpp
 * @brief ITU-R BS.1770-4 loudness, sample and true peak, and stereo metrics for one signal point.
 */

namespace magda::sdk {

/** @brief One reading of a measurer; the loudness meter's payload. */
using TrackMeasurementSnapshot = LevelsSnapshot;

/**
 * @brief Momentary, short-term and gated-integrated LUFS, sample and optional 4x true peak,
 *        correlation, width and the PLR/PSR figures derived from them.
 *
 * Audio thread: @ref process, allocation-free after @ref prepare. Message thread: @ref read,
 * lock-free. The integrated histogram is read with benign races (monotonic counts, so a torn
 * read is a slightly stale value).
 */
class TrackMeasurer {
  public:
    /**
     * @brief Set the feed rate and clear every window.
     *
     * @param sampleRate Feed rate.
     * @param maxBlockSize Unused; @ref process takes any block length.
     * @param enableTruePeak Run the 4x oversampler, the costly part of a reading.
     */
    void prepare(double sampleRate, int maxBlockSize, bool enableTruePeak) {
        static_cast<void>(maxBlockSize);
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        enableTruePeak_ = enableTruePeak;
        computeKWeightingCoeffs(sampleRate_);
        if (enableTruePeak_) {
            tpOversamplerL_.prepare(kOsFactor, kTapsPerPhase);
            tpOversamplerR_.prepare(kOsFactor, kTapsPerPhase);
        }
        blockSamples_ = std::max(1, static_cast<int>(std::lround(sampleRate_ * 0.1)));  // 100 ms
        reset();
    }

    /** @brief Clear the gating history and every running window; not concurrent with process(). */
    void reset() {
        for (auto& f : filtL_)
            f = {};
        for (auto& f : filtR_)
            f = {};
        blockEnergy_.fill(0.0);
        blockCount_ = 0;
        curBlockAcc_ = 0.0;
        curBlockN_ = 0;
        for (auto& h : histogram_)
            h.store(0, std::memory_order_relaxed);
        corrSmoothed_ = 1.0f;
        widthSmoothed_ = 0.0f;
        tpOversamplerL_.reset();
        tpOversamplerR_.reset();
        momentary_.store(kSilenceLufs, std::memory_order_relaxed);
        shortTerm_.store(kSilenceLufs, std::memory_order_relaxed);
        samplePeak_.store(kSilenceDb, std::memory_order_relaxed);
        truePeak_.store(kSilenceDb, std::memory_order_relaxed);
        correlation_.store(1.0f, std::memory_order_relaxed);
        width_.store(0.0f, std::memory_order_relaxed);
        valid_.store(false, std::memory_order_relaxed);
    }

    /** @brief Audio thread. Measure one block; a single channel is treated as L == R. */
    void process(ConstBufferView audio) noexcept {
        const int numChannels = audio.numChannels();
        const int numSamples = audio.numFrames();
        if (numChannels <= 0 || numSamples <= 0)
            return;
        const float* l = audio.channel(0);
        const float* r = numChannels > 1 ? audio.channel(1) : audio.channel(0);

        double sumMidSq = 0.0, sumSideSq = 0.0, sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

        // Its own reduction: the loop below is serial on the K-weighting recursion.
        const float samplePeak =
            numChannels > 1 ? std::max(peakMagnitude(l, numSamples), peakMagnitude(r, numSamples))
                            : peakMagnitude(l, numSamples);

        for (int i = 0; i < numSamples; ++i) {
            const float xl = l[i];
            const float xr = r[i];

            const double kl = applyKWeight(filtL_, xl);
            const double kr = applyKWeight(filtR_, xr);
            curBlockAcc_ += kl * kl + kr * kr;  // stereo channel weights = 1.0
            if (++curBlockN_ >= blockSamples_)
                closeGatingBlock();

            const double mid = 0.5 * (xl + xr);
            const double side = 0.5 * (xl - xr);
            sumMidSq += mid * mid;
            sumSideSq += side * side;
            sumLR += static_cast<double>(xl) * xr;
            sumLL += static_cast<double>(xl) * xl;
            sumRR += static_cast<double>(xr) * xr;
        }

        if (captureSpectrum_.load(std::memory_order_acquire)) {
            const float* const pair[2] = {l, r};
            spectrumRing_.writeDownmix(ConstBufferView(pair, std::min(numChannels, 2), numSamples));
        }

        if (samplePeak > 0.0f)
            publishMax(samplePeak_, linearToDb(samplePeak));

        if (enableTruePeak_) {
            const float tp = std::max(oversamplePeak(tpOversamplerL_, l, numSamples),
                                      oversamplePeak(tpOversamplerR_, r, numSamples));
            if (tp > 0.0f)
                publishMax(truePeak_, linearToDb(tp));
        }

        // Normalised cross-correlation over the block, one-pole smoothed.
        const double denom = std::sqrt(sumLL * sumRR);
        const float instCorr =
            denom > 1.0e-12 ? static_cast<float>(std::clamp(sumLR / denom, -1.0, 1.0)) : 1.0f;
        corrSmoothed_ += kCorrSmooth * (instCorr - corrSmoothed_);
        correlation_.store(corrSmoothed_, std::memory_order_relaxed);

        // Side energy over mid+side: bounded [0, 1], and 1 at the anti-phase limit.
        const double msTotal = sumMidSq + sumSideSq;
        const float instWidth = msTotal > 1.0e-12 ? static_cast<float>(sumSideSq / msTotal) : 0.0f;
        widthSmoothed_ += kCorrSmooth * (instWidth - widthSmoothed_);
        width_.store(widthSmoothed_, std::memory_order_relaxed);

        momentary_.store(windowLufs(4), std::memory_order_relaxed);
        shortTerm_.store(windowLufs(30), std::memory_order_relaxed);
        valid_.store(true, std::memory_order_relaxed);
    }

    /** @brief Capture a mono downmix into the spectrum ring; off by default, one branch when off. */
    void setSpectrumCaptureEnabled(bool shouldCapture) noexcept {
        captureSpectrum_.store(shouldCapture, std::memory_order_release);
    }

    bool spectrumCaptureEnabled() const noexcept {
        return captureSpectrum_.load(std::memory_order_acquire);
    }

    /** @brief Message thread. The captured downmix, for band analysis. */
    const engine::SampleRing& getSpectrumRing() const noexcept {
        return spectrumRing_;
    }

    double sampleRate() const noexcept {
        return sampleRate_;
    }

    /** @brief Message thread. Lock-free snapshot of the current measurements. */
    TrackMeasurementSnapshot read() const noexcept {
        TrackMeasurementSnapshot s;
        s.valid = valid_.load(std::memory_order_relaxed);
        s.momentaryLufs = momentary_.load(std::memory_order_relaxed);
        s.shortTermLufs = shortTerm_.load(std::memory_order_relaxed);
        s.integratedLufs = computeIntegrated();
        s.samplePeakDb = samplePeak_.load(std::memory_order_relaxed);
        s.truePeakValid = enableTruePeak_;
        s.truePeakDb = enableTruePeak_ ? truePeak_.load(std::memory_order_relaxed) : kSilenceDb;
        s.correlation = correlation_.load(std::memory_order_relaxed);
        s.width = width_.load(std::memory_order_relaxed);

        // Dynamics: peak (true peak when available) above loudness.
        const float peak =
            s.truePeakValid && s.truePeakDb > kSilenceDb ? s.truePeakDb : s.samplePeakDb;
        s.plrValid = peak > kSilenceDb && s.integratedLufs > kSilenceLufs;
        s.psrValid = peak > kSilenceDb && s.shortTermLufs > kSilenceLufs;
        if (s.plrValid)
            s.plr = peak - s.integratedLufs;
        if (s.psrValid)
            s.psr = peak - s.shortTermLufs;
        return s;
    }

    bool truePeakEnabled() const noexcept {
        return enableTruePeak_;
    }

  private:
    // K-weighting: two cascaded biquads, [0] the high shelf and [1] the RLB high-pass.
    BiquadCoeffs<double> preFilter_;
    BiquadCoeffs<double> highPass_;
    std::array<BiquadState<double>, 2> filtL_{};
    std::array<BiquadState<double>, 2> filtR_{};

    // Coefficients follow the libebur128 design, so LUFS holds at any sample rate.
    void computeKWeightingCoeffs(double fs) {
        const double Vh = std::pow(10.0, 3.999843853973347 / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        preFilter_ = biquad::highShelfK(fs, 1681.974450955533, Vh, Vb, 0.7071752369554196);
        highPass_ = biquad::highPassK(fs, 38.13547087602444, 0.5003270373238773);
    }

    double applyKWeight(std::array<BiquadState<double>, 2>& st, double x) const noexcept {
        return processBiquad(highPass_, st[1], processBiquad(preFilter_, st[0], x));
    }

    // 100 ms gating blocks feed momentary, short-term and integrated.
    static constexpr int kRing = 30;           // 30 * 100 ms = the 3 s short-term window
    std::array<double, kRing> blockEnergy_{};  // mean-square per block
    int blockCount_ = 0;                       // blocks closed since reset
    double curBlockAcc_ = 0.0;
    int curBlockN_ = 0;
    int blockSamples_ = 4800;

    // Integrated histogram: block loudness in 0.1 LU bins from -70 to +5 LUFS.
    static constexpr int kHistBins = 751;
    static constexpr double kHistMin = -70.0;
    static constexpr double kHistStep = 0.1;
    std::array<std::atomic<std::uint32_t>, kHistBins> histogram_{};

    void closeGatingBlock() noexcept {
        const double meanSq = curBlockN_ > 0 ? curBlockAcc_ / curBlockN_ : 0.0;
        blockEnergy_[static_cast<std::size_t>(blockCount_ % kRing)] = meanSq;
        ++blockCount_;
        curBlockAcc_ = 0.0;
        curBlockN_ = 0;

        // Integrated: a 400 ms gating block is the last four 100 ms blocks, gated at -70 LUFS.
        if (blockCount_ >= 4) {
            double e = 0.0;
            for (int k = 0; k < 4; ++k)
                e += blockEnergy_[static_cast<std::size_t>((blockCount_ - 1 - k) % kRing)];
            const double meanSq4 = e / 4.0;
            if (meanSq4 > 0.0) {
                const double loud = -0.691 + 10.0 * std::log10(meanSq4);
                if (loud >= -70.0) {
                    int bin = static_cast<int>(std::lround((loud - kHistMin) / kHistStep));
                    bin = std::clamp(bin, 0, kHistBins - 1);
                    histogram_[static_cast<std::size_t>(bin)].fetch_add(
                        1, std::memory_order_relaxed);
                }
            }
        }
    }

    /// Loudness over the last @p nBlocks 100 ms blocks.
    float windowLufs(int nBlocks) const noexcept {
        const int n = std::min({nBlocks, blockCount_, kRing});
        if (n <= 0)
            return kSilenceLufs;
        double e = 0.0;
        for (int k = 0; k < n; ++k)
            e += blockEnergy_[static_cast<std::size_t>((blockCount_ - 1 - k) % kRing)];
        const double meanSq = e / n;
        if (meanSq <= 0.0)
            return kSilenceLufs;
        return static_cast<float>(-0.691 + 10.0 * std::log10(meanSq));
    }

    /// Gated integrated loudness from the histogram: absolute -70 LUFS, then relative -10 LU.
    float computeIntegrated() const noexcept {
        double sumE = 0.0;
        std::uint64_t count = 0;
        for (int b = 0; b < kHistBins; ++b) {
            const std::uint32_t c =
                histogram_[static_cast<std::size_t>(b)].load(std::memory_order_relaxed);
            if (c == 0)
                continue;
            const double loud = kHistMin + b * kHistStep;
            const double energy = std::pow(10.0, (loud + 0.691) / 10.0);
            sumE += energy * c;
            count += c;
        }
        if (count == 0)
            return kSilenceLufs;
        const double ungated = -0.691 + 10.0 * std::log10(sumE / static_cast<double>(count));
        const double relGate = ungated - 10.0;

        double sumE2 = 0.0;
        std::uint64_t count2 = 0;
        for (int b = 0; b < kHistBins; ++b) {
            const double loud = kHistMin + b * kHistStep;
            if (loud < relGate)
                continue;
            const std::uint32_t c =
                histogram_[static_cast<std::size_t>(b)].load(std::memory_order_relaxed);
            if (c == 0)
                continue;
            const double energy = std::pow(10.0, (loud + 0.691) / 10.0);
            sumE2 += energy * c;
            count2 += c;
        }
        if (count2 == 0)
            return kSilenceLufs;
        return static_cast<float>(-0.691 + 10.0 * std::log10(sumE2 / static_cast<double>(count2)));
    }

    static constexpr int kOsFactor = 4;
    static constexpr int kTapsPerPhase = 12;

    PolyphaseUpsampler tpOversamplerL_;
    PolyphaseUpsampler tpOversamplerR_;

    static float oversamplePeak(PolyphaseUpsampler& upsampler, const float* x, int n) noexcept {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            upsampler.processSample(x[i], [&peak](float y) { peak = std::max(peak, std::abs(y)); });
        return peak;
    }

    static float linearToDb(float lin) noexcept {
        return lin > 1.0e-9f ? 20.0f * std::log10(lin) : kSilenceDb;
    }

    static void publishMax(std::atomic<float>& slot, float candidate) noexcept {
        float cur = slot.load(std::memory_order_relaxed);
        while (candidate > cur &&
               !slot.compare_exchange_weak(cur, candidate, std::memory_order_relaxed)) {
        }
    }

    static constexpr float kCorrSmooth = 0.2f;  // one-pole smoothing per block

    double sampleRate_ = 48000.0;
    bool enableTruePeak_ = false;
    float corrSmoothed_ = 1.0f, widthSmoothed_ = 0.0f;

    std::atomic<float> momentary_{kSilenceLufs};
    std::atomic<float> shortTerm_{kSilenceLufs};
    std::atomic<float> samplePeak_{kSilenceDb};
    std::atomic<float> truePeak_{kSilenceDb};
    std::atomic<float> correlation_{1.0f};
    std::atomic<float> width_{0.0f};
    std::atomic<bool> valid_{false};

    std::atomic<bool> captureSpectrum_{false};
    engine::SampleRing spectrumRing_{4096};  // >= one 2048-point FFT frame
};

}  // namespace magda::sdk
