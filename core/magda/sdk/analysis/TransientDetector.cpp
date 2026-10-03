#include "magda/sdk/analysis/TransientDetector.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <magda/sdk/audio/BlockPeak.hpp>

namespace magda::sdk {

namespace {

/// Read block size; a trigger's rewind is clamped to the start of its block, so changing it
/// moves detected markers.
constexpr int kBlockSamples = 32768;

/// A one-pole follower with separate attack and release, where 1 is instant and 0 never moves.
class EnvelopeFollower {
  public:
    void setCoefficients(float attack, float release) noexcept {
        attack_ = attack;
        release_ = release;
    }

    void process(float* samples, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const auto in = std::abs(samples[i]);

            if (envelope_ < in)
                envelope_ += attack_ * (in - envelope_);
            else if (envelope_ > in)
                envelope_ -= release_ * (envelope_ - in);

            samples[i] = envelope_;
        }
    }

  private:
    float envelope_ = 0.0f;
    float attack_ = 1.0f;
    float release_ = 1.0f;
};

/// Sample-to-sample difference, which peaks where the envelope climbs fastest: the onset.
class Differentiator {
  public:
    void process(float* samples, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const auto current = samples[i];
            samples[i] = current - last_;
            last_ = current;
        }
    }

  private:
    float last_ = 0.0f;
};

/// The source's own peak, which the threshold is relative to.
float peakOf(const TransientBlockReader& read, std::vector<float>& block,
             std::int64_t totalSamples) {
    float peak = 0.0f;

    for (std::int64_t at = 0; at < totalSamples; at += kBlockSamples) {
        const auto count =
            static_cast<int>(std::min<std::int64_t>(kBlockSamples, totalSamples - at));

        if (read(block.data(), at, count) <= 0)
            continue;

        peak = std::max(peak, peakMagnitude(block.data(), count));
    }

    return peak;
}

/// Thin @p transients until none are closer than @p spacing: backwards, keeping the later of
/// a pair, and repeated because a pass can leave a new pair adjacent. Gives up after ten.
void thin(std::vector<double>& transients, double spacing) {
    if (transients.size() < 2)
        return;

    for (int pass = 0; pass < 10; ++pass) {
        const auto before = transients.size();
        auto last = transients.back();

        for (int i = static_cast<int>(transients.size()) - 2; i >= 0; --i) {
            const auto at = transients[static_cast<std::size_t>(i)];

            if (last - at < spacing)
                transients.erase(transients.begin() + i);
            else
                last = at;
        }

        if (transients.size() == before)
            return;
    }
}

}  // namespace

std::vector<double> detectTransients(const TransientBlockReader& read, std::int64_t totalFrames,
                                     double sampleRate,
                                     const TransientDetectionSettings& settings) {
    std::vector<double> transients;

    if (!(sampleRate > 0.0) || totalFrames <= 0 || !read)
        return transients;

    std::vector<float> block(kBlockSamples, 0.0f);

    const auto peak = peakOf(read, block, totalFrames);
    const auto scale = peak > 0.0f ? 1.0f / peak : 1.0f;

    EnvelopeFollower followers[3];
    for (auto& follower : followers)
        follower.setCoefficients(1.0f, 0.002f);

    Differentiator differentiator;

    // Single-precision pow, as juce::Decibels::decibelsToGain<float>, so markers do not move.
    const auto threshold =
        std::pow(10.0f, (-10.0f - std::clamp(settings.sensitivity, 0.0f, 1.0f) * 30.0f) * 0.05f);
    const auto lockout = static_cast<int>(sampleRate * settings.retriggerSeconds);

    // Half a millisecond: the differential of a smoothed envelope peaks a little after the
    // attack, and the marker belongs where the sound starts. Rounded up (23 samples at
    // 44100, not 22).
    const auto rewind = static_cast<int>(std::ceil(sampleRate * 0.0005));

    int countdown = 0;

    for (std::int64_t at = 0; at < totalFrames; at += kBlockSamples) {
        const auto count =
            static_cast<int>(std::min<std::int64_t>(kBlockSamples, totalFrames - at));

        read(block.data(), at, count);

        auto* samples = block.data();
        for (int i = 0; i < count; ++i)
            samples[i] *= scale;

        followers[0].process(samples, count);
        followers[1].process(samples, count);
        differentiator.process(samples, count);
        followers[2].process(samples, count);

        for (int i = 0; i < count; ++i) {
            if (countdown > 0)
                --countdown;

            if (samples[i] > threshold) {
                if (countdown == 0)
                    transients.push_back(static_cast<double>(at + std::max(0, i - rewind)) /
                                         sampleRate);

                countdown = lockout;
            }
        }
    }

    thin(transients, settings.minimumSpacingSeconds);

    return transients;
}

std::vector<double> detectTransients(ConstBufferView audio, double sampleRate,
                                     const TransientDetectionSettings& settings) {
    if (audio.numChannels() <= 0)
        return {};

    const float* source = audio.channel(0);
    const auto reader = [source, total = audio.numFrames()](float* destination,
                                                           std::int64_t start, int count) {
        const auto available = static_cast<int>(
            std::clamp<std::int64_t>(total - start, 0, static_cast<std::int64_t>(count)));
        if (available > 0)
            std::memcpy(destination, source + start, static_cast<std::size_t>(available) * sizeof(float));
        std::fill(destination + available, destination + count, 0.0f);
        return available;
    };
    return detectTransients(reader, audio.numFrames(), sampleRate, settings);
}

}  // namespace magda::sdk
