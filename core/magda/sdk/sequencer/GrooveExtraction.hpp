#pragma once

#include <magda/sdk/audio/BufferView.hpp>
#include <optional>
#include <span>
#include <vector>

/**
 * @file GrooveExtraction.hpp
 * @brief A groove read off a recorded loop: how far each hit sits from the straight grid.
 *
 * The hits are the loop's transients (detectTransients). Each one is placed on its nearest grid
 * step and measured against it; the groove is the mean shift at each step of a figure (one bar
 * by default), over every repeat of the figure in the loop. The result is grooveTimeBend()'s
 * input, so a recorded feel loads as any other groove does, and AMOUNT sets how much is heard.
 * Offline; allocates.
 */

namespace magda::sdk::sequencer {

/** @brief A loop's timing as the extractor is told it. */
struct GrooveExtractionSettings {
    /// The meter.
    double beatsPerBar = 4.0;
    /// The loop's length in bars.
    int bars = 1;
    /// The loop's tempo; 0 reads it from the loop's length, which is then exactly @c bars bars.
    double bpm = 0.0;
    /// The grid: 2 steps a beat for eighths, 4 for sixteenths.
    int notesPerBeat = 4;
    /// The groove's length in steps, repeated across the loop; 0 is one bar.
    int figureSteps = 0;
    /// The transient detector's sensitivity: 0 finds few hits, 1 many.
    float sensitivity = 0.5f;

    bool operator==(const GrooveExtractionSettings&) const = default;
};

/** @brief One hit of the loop: where it starts, and how loud it is. */
struct GrooveHit {
    double seconds = 0.0;
    /// Any linear measure; only the ratios between hits are read.
    float level = 1.0f;
};

/** @brief A groove read off a loop, one entry per step of the figure. */
struct ExtractedGroove {
    /// The shift of each step, -1 to 1 of a step: grooveTimeBend()'s lateness. 0 where no hit.
    std::vector<float> lateness;
    /// The mean level of each step's hits, 1 at the loudest step, 0 where no hit.
    std::vector<float> accent;
    /// How many hits each step was measured from.
    std::vector<int> hits;
    /// The tempo the grid was laid at.
    double bpm = 0.0;
    /// Where the grid's first step sits in the loop, in seconds.
    double originSeconds = 0.0;
};

/**
 * @brief The groove of @p hits in a loop of @p durationSeconds.
 *
 * The grid starts at the first hit when that hit is within half a step of the loop's start
 * (a detector reads every onset a little early, alike, so the shifts stay true), else at 0.
 * A hit belongs to its nearest step, and of two hits on one step the nearer counts; a hit
 * past the loop's last step is the next repeat's downbeat, early. nullopt for settings that
 * lay no grid (no bars, no steps, no tempo), or no hits.
 */
std::optional<ExtractedGroove> extractGroove(std::span<const GrooveHit> hits,
                                             double durationSeconds,
                                             const GrooveExtractionSettings& settings);

/**
 * @brief The groove of a loop in memory: its transients, each hit's level its peak over the
 * first 50 ms or up to the next hit. Only the first channel is analysed.
 */
std::optional<ExtractedGroove> extractGroove(ConstBufferView audio, double sampleRate,
                                             const GrooveExtractionSettings& settings);

}  // namespace magda::sdk::sequencer
