#include "magda/sdk/sequencer/GrooveExtraction.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <magda/sdk/analysis/TransientDetector.hpp>

namespace magda::sdk::sequencer {

namespace {

/// How long a hit's level is read over, at most: its attack, not its tail.
constexpr double kLevelSeconds = 0.05;

/// The grid a loop lays: its step length, its steps and the figure's.
struct Grid {
    double stepSeconds = 0.0;
    double bpm = 0.0;
    std::int64_t steps = 0;
    int figure = 0;
};

std::optional<Grid> layGrid(double durationSeconds, const GrooveExtractionSettings& settings) {
    if (!(settings.beatsPerBar > 0.0) || settings.bars <= 0 || settings.notesPerBeat <= 0 ||
        !(durationSeconds > 0.0) || !(settings.bpm >= 0.0))
        return std::nullopt;

    const double beats = settings.beatsPerBar * settings.bars;
    Grid grid;
    grid.bpm = settings.bpm > 0.0 ? settings.bpm : beats * 60.0 / durationSeconds;
    grid.stepSeconds = 60.0 / (grid.bpm * settings.notesPerBeat);
    grid.steps = std::llround(beats * settings.notesPerBeat);
    grid.figure = settings.figureSteps > 0
                      ? settings.figureSteps
                      : static_cast<int>(std::lround(settings.beatsPerBar * settings.notesPerBeat));
    if (grid.steps <= 0 || grid.figure <= 0 || !std::isfinite(grid.stepSeconds))
        return std::nullopt;
    return grid;
}

}  // namespace

std::optional<ExtractedGroove> extractGroove(std::span<const GrooveHit> hits,
                                             double durationSeconds,
                                             const GrooveExtractionSettings& settings) {
    const auto grid = layGrid(durationSeconds, settings);
    if (!grid || hits.empty())
        return std::nullopt;

    const auto first = std::min_element(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        return a.seconds < b.seconds;
    });
    const double origin = first->seconds < 0.5 * grid->stepSeconds ? first->seconds : 0.0;

    // Each absolute step's nearest hit: its offset in steps and its level.
    struct Nearest {
        double offset = 0.0;
        float level = 0.0f;
        bool set = false;
    };
    std::vector<Nearest> nearest(static_cast<std::size_t>(grid->steps));
    for (const auto& hit : hits) {
        const double position = (hit.seconds - origin) / grid->stepSeconds;
        if (!std::isfinite(position) || position < -0.5)
            continue;
        const auto step = std::llround(position);
        const double offset = position - static_cast<double>(step);
        auto& slot = nearest[static_cast<std::size_t>(step % grid->steps)];
        if (!slot.set || std::abs(offset) < std::abs(slot.offset))
            slot = {offset, hit.level, true};
    }

    const auto figure = static_cast<std::size_t>(grid->figure);
    std::vector<double> shift(figure, 0.0), level(figure, 0.0);
    ExtractedGroove groove;
    groove.hits.assign(figure, 0);
    for (std::int64_t step = 0; step < grid->steps; ++step) {
        const auto& slot = nearest[static_cast<std::size_t>(step)];
        if (!slot.set)
            continue;
        const auto at = static_cast<std::size_t>(step % grid->figure);
        shift[at] += 2.0 * slot.offset;
        level[at] += std::max(0.0f, slot.level);
        ++groove.hits[at];
    }

    double loudest = 0.0;
    for (std::size_t i = 0; i < figure; ++i)
        if (groove.hits[i] > 0)
            loudest = std::max(loudest, level[i] / groove.hits[i]);

    groove.lateness.assign(figure, 0.0f);
    groove.accent.assign(figure, 0.0f);
    for (std::size_t i = 0; i < figure; ++i) {
        if (groove.hits[i] == 0)
            continue;
        groove.lateness[i] = static_cast<float>(std::clamp(shift[i] / groove.hits[i], -1.0, 1.0));
        groove.accent[i] =
            loudest > 0.0 ? static_cast<float>(level[i] / groove.hits[i] / loudest) : 0.0f;
    }
    groove.bpm = grid->bpm;
    groove.originSeconds = origin;
    return groove;
}

std::optional<ExtractedGroove> extractGroove(ConstBufferView audio, double sampleRate,
                                             const GrooveExtractionSettings& settings) {
    if (audio.numChannels() <= 0 || audio.numFrames() <= 0 || !(sampleRate > 0.0))
        return std::nullopt;

    const double duration = audio.numFrames() / sampleRate;
    const auto grid = layGrid(duration, settings);
    if (!grid)
        return std::nullopt;

    // Hits closer than half a step are one hit; the detector's own spacing would merge fast
    // sixteenths.
    TransientDetectionSettings detection;
    detection.sensitivity = settings.sensitivity;
    detection.minimumSpacingSeconds =
        std::min(detection.minimumSpacingSeconds, 0.5 * grid->stepSeconds);
    detection.retriggerSeconds = std::min(detection.retriggerSeconds, 0.4 * grid->stepSeconds);
    const auto onsets = detectTransients(audio, sampleRate, detection);

    const float* samples = audio.channel(0);
    std::vector<GrooveHit> hits;
    hits.reserve(onsets.size());
    for (std::size_t i = 0; i < onsets.size(); ++i) {
        const double end =
            std::min(onsets[i] + kLevelSeconds, i + 1 < onsets.size() ? onsets[i + 1] : duration);
        const auto from =
            std::clamp<std::int64_t>(std::llround(onsets[i] * sampleRate), 0, audio.numFrames());
        const auto to =
            std::clamp<std::int64_t>(std::llround(end * sampleRate), from, audio.numFrames());
        float peak = 0.0f;
        for (auto at = from; at < to; ++at)
            peak = std::max(peak, std::abs(samples[at]));
        hits.push_back({onsets[i], peak});
    }
    return extractGroove(hits, duration, settings);
}

}  // namespace magda::sdk::sequencer
