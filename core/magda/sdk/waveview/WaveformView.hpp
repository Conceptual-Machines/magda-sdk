#pragma once

#include <cstdint>

#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/peaks/PeakData.hpp"

/**
 * @file WaveformView.hpp
 * @brief Zoom, scroll, markers and playhead over a sample, drawn as a display list
 *        (docs/waveform-view.md).
 */

namespace magda::sdk {

/// What a waveform view draws from: the loudest absolute sample over a range.
class WaveformSource {
  public:
    virtual ~WaveformSource() = default;
    virtual std::int64_t numSamples() const = 0;
    /// Over [@p start, @p end), already clamped to the source; 0 for an empty range.
    virtual float absolutePeak(std::int64_t start, std::int64_t end) const = 0;
};

/// Exact, from one channel of samples the caller keeps alive.
class BufferWaveformSource final : public WaveformSource {
  public:
    BufferWaveformSource(const float* samples, std::int64_t numSamples)
        : samples_(samples), numSamples_(numSamples) {}
    std::int64_t numSamples() const override {
        return numSamples_;
    }
    float absolutePeak(std::int64_t start, std::int64_t end) const override;

  private:
    const float* samples_;
    std::int64_t numSamples_;
};

/// From stored peaks: 64-sample buckets, so coarser than the samples when zoomed far in.
class PeakDataWaveformSource final : public WaveformSource {
  public:
    PeakDataWaveformSource(const PeakData& peaks, int channel) : peaks_(peaks), channel_(channel) {}
    std::int64_t numSamples() const override {
        return peaks_.numSourceSamples();
    }
    float absolutePeak(std::int64_t start, std::int64_t end) const override;

  private:
    const PeakData& peaks_;
    int channel_;
};

/// Positions in source seconds.
struct WaveformMarkers {
    double start = 0.0;
    double end = 0.0;
    bool loop = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;
};

struct WaveformPointer {
    float x = 0.0f;
    float y = 0.0f;
    bool shift = false;
    /// Cmd on macOS, Ctrl elsewhere.
    bool command = false;
    bool alt = false;
    bool middle = false;
};

enum class WaveformCursor { Normal, ResizeLeftRight, DraggingHand, Zoom, ZoomIn, ZoomOut };

struct WaveformResponse {
    bool repaint = false;
    /// A marker moved; read markers().
    bool markersChanged = false;
};

/**
 * @brief The state and gestures of a zoomable waveform pane, in pane pixels, origin top left.
 *
 * Zoom is pixels per second of source; the narrowest zoom fits the whole source.
 */
class WaveformView {
  public:
    static constexpr double kMaxPixelsPerSecond = 5000.0;

    void setSize(int width, int height);
    /// Fits the source to the width and scrolls home. Null clears. The source must outlive its use.
    void setSource(const WaveformSource* source, double lengthSeconds);
    bool hasSource() const {
        return source_ != nullptr && lengthSeconds_ > 0.0;
    }
    void setGain(float gain) {
        gain_ = gain;
    }
    void setMarkers(const WaveformMarkers& markers) {
        markers_ = markers;
    }
    const WaveformMarkers& markers() const {
        return markers_;
    }
    /// Drawn while above zero.
    void setPlayhead(double seconds) {
        playhead_ = seconds;
    }

    double pixelsPerSecond() const {
        return pixelsPerSecond_;
    }
    double scrollSeconds() const {
        return scrollSeconds_;
    }

    WaveformResponse pointerDown(const WaveformPointer& pointer);
    WaveformResponse pointerDrag(const WaveformPointer& pointer);
    WaveformResponse pointerUp(const WaveformPointer& pointer);
    /// Zooms by @p factor keeping the time under @p anchorX in place.
    WaveformResponse zoomBy(double factor, float anchorX);

    WaveformCursor cursor(const WaveformPointer& pointer) const;

    /// The pixel a time draws at, kept inside the pane so the end markers stay visible.
    float secondsToPixel(double seconds) const;
    double pixelToSeconds(float x) const;

    /// Replaces @p out with the current frame; empty without a source.
    void render(display::DisplayList& out) const;

  private:
    enum class Drag { None, SampleStart, SampleEnd, LoopStart, LoopEnd, LoopRegion, Scroll, Zoom };

    Drag hitTest(int x, int y) const;
    double minPixelsPerSecond() const;
    void clampScroll();
    void setMarker(Drag drag, double seconds);

    const WaveformSource* source_ = nullptr;
    double lengthSeconds_ = 0.0;
    int width_ = 0;
    int height_ = 0;
    float gain_ = 1.0f;
    WaveformMarkers markers_;
    double playhead_ = 0.0;
    double pixelsPerSecond_ = 0.0;
    double scrollSeconds_ = 0.0;

    Drag drag_ = Drag::None;
    int downX_ = 0;
    int downY_ = 0;
    double dragStartScroll_ = 0.0;
    double dragStartPixelsPerSecond_ = 0.0;
    double zoomAnchorSeconds_ = 0.0;
    int zoomAnchorX_ = 0;
    double loopDragStart_ = 0.0;
    double loopDragEnd_ = 0.0;
    int zoomDirection_ = 0;
};

}  // namespace magda::sdk
