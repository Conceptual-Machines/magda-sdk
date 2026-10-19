#include "magda/sdk/waveview/WaveformView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace magda::sdk {

namespace {

using display::Colour;
using display::ColourRole;

constexpr int kMarkerHitPixels = 5;
constexpr int kLoopBarHeight = 8;
constexpr int kWaveInset = 2;
constexpr int kFallbackWidth = 200;
constexpr int kThumbEdgePixels = 4;
constexpr float kMinThumbPixels = 12.0f;
constexpr double kRulerSpacing = 80.0;  ///< The narrowest gap between labelled ticks.
constexpr float kMajorTick = 6.0f;
constexpr float kMinorTick = 3.0f;
constexpr float kRulerLabelWidth = 70.0f;

/// juce::jlimit, which tolerates inverted bounds.
template <typename T> T limit(T lower, T upper, T value) {
    return value < lower ? lower : (upper < value ? upper : value);
}

int toPixel(float v) {
    return static_cast<int>(std::lround(v));
}

/// The 1-2-5 step at least @p raw units wide, and its minor ticks per step.
std::pair<double, int> rulerStep(double raw, bool wholeUnits) {
    raw = std::max(raw, wholeUnits ? 1.0 : 1e-4);
    const double decade = std::pow(10.0, std::floor(std::log10(raw)));
    double step = 10.0 * decade;
    int divisions = 5;
    if (decade >= raw * (1.0 - 1e-9))
        step = decade;
    else if (2.0 * decade >= raw)
        step = 2.0 * decade, divisions = 4;
    else if (5.0 * decade >= raw)
        step = 5.0 * decade;
    // A sample is the smallest tick.
    if (wholeUnits && step < divisions)
        divisions = static_cast<int>(step);
    return {step, divisions};
}

std::string rulerLabel(double units, double step, bool samples) {
    char text[32];
    if (samples) {
        std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(std::llround(units)));
        return text;
    }
    const int decimals =
        step >= 1.0 ? 0 : std::clamp(static_cast<int>(std::ceil(-std::log10(step) - 1e-9)), 0, 4);
    if (units < 60.0) {
        std::snprintf(text, sizeof(text), "%.*fs", decimals, units);
        return text;
    }
    const auto minutes = static_cast<int>(units / 60.0);
    const double seconds = units - 60.0 * minutes;
    std::snprintf(text, sizeof(text), "%d:%0*.*f", minutes, decimals > 0 ? decimals + 3 : 2,
                  decimals, seconds);
    return text;
}

}  // namespace

float BufferWaveformSource::absolutePeak(std::int64_t start, std::int64_t end) const {
    float peak = 0.0f;
    for (auto s = start; s < end; ++s)
        peak = std::max(peak, std::abs(samples_[s]));
    return peak;
}

float PeakDataWaveformSource::absolutePeak(std::int64_t start, std::int64_t end) const {
    if (end <= start)
        return 0.0f;
    const auto range = peaks_.getMinMaxForRange(channel_, start, end);
    return std::max(std::abs(range.min), std::abs(range.max));
}

void WaveformView::setSize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    if (hasSource())
        pixelsPerSecond_ = std::max(pixelsPerSecond_, minPixelsPerSecond());
}

void WaveformView::setSource(const WaveformSource* source, double lengthSeconds) {
    source_ = source;
    lengthSeconds_ = source != nullptr ? lengthSeconds : 0.0;
    drag_ = Drag::None;
    if (!hasSource())
        return;
    const int width = width_ > 0 ? width_ : kFallbackWidth;
    pixelsPerSecond_ = static_cast<double>(width) / lengthSeconds_;
    scrollSeconds_ = 0.0;
}

double WaveformView::minPixelsPerSecond() const {
    return static_cast<double>(width_) / lengthSeconds_;
}

int WaveformView::waveTop() const {
    return chrome_.ruler ? std::min(kRulerHeight, height_) : 0;
}

int WaveformView::waveBottom() const {
    return chrome_.scrollBar ? std::max(waveTop(), height_ - kScrollBarHeight) : height_;
}

bool WaveformView::inScrollBar(int y) const {
    return chrome_.scrollBar && width_ > 0 && y >= waveBottom();
}

bool WaveformView::overRuler(const WaveformPointer& pointer) const {
    return chrome_.ruler && hasSource() && toPixel(pointer.y) < waveTop();
}

std::pair<float, float> WaveformView::thumb() const {
    const double scale = static_cast<double>(width_) / lengthSeconds_;
    const double visible = std::min(lengthSeconds_, static_cast<double>(width_) / pixelsPerSecond_);
    const float width = std::max(kMinThumbPixels, static_cast<float>(visible * scale));
    const float left =
        std::min(static_cast<float>(scrollSeconds_ * scale), static_cast<float>(width_) - width);
    return {std::max(0.0f, left), width};
}

WaveformView::Drag WaveformView::scrollBarHitTest(int x) const {
    const auto [left, width] = thumb();
    const auto fx = static_cast<float>(x);
    if (std::abs(fx - left) <= kThumbEdgePixels)
        return Drag::ThumbStart;
    if (std::abs(fx - (left + width)) <= kThumbEdgePixels)
        return Drag::ThumbEnd;
    return fx > left && fx < left + width ? Drag::Thumb : Drag::None;
}

// The thumb's span is the visible time: moving it scrolls, moving an edge zooms against the other.
void WaveformView::dragThumb(Drag drag, int x) {
    const double delta = static_cast<double>(x - downX_) * lengthSeconds_ / width_;
    const double start = dragStartScroll_;
    const double end = start + static_cast<double>(width_) / dragStartPixelsPerSecond_;
    const double narrowest = static_cast<double>(width_) / kMaxPixelsPerSecond;
    switch (drag) {
        case Drag::Thumb:
            scrollSeconds_ = start + delta;
            break;
        case Drag::ThumbStart:
            scrollSeconds_ = limit(0.0, end - narrowest, start + delta);
            pixelsPerSecond_ = static_cast<double>(width_) / (end - scrollSeconds_);
            break;
        case Drag::ThumbEnd:
            pixelsPerSecond_ = static_cast<double>(width_) /
                               (limit(start + narrowest, lengthSeconds_, end + delta) - start);
            break;
        default:
            return;
    }
    pixelsPerSecond_ = limit(minPixelsPerSecond(), kMaxPixelsPerSecond, pixelsPerSecond_);
    clampScroll();
}

void WaveformView::clampScroll() {
    const double visible = static_cast<double>(width_) / pixelsPerSecond_;
    scrollSeconds_ = limit(0.0, std::max(0.0, lengthSeconds_ - visible), scrollSeconds_);
}

float WaveformView::secondsToPixel(double seconds) const {
    if (lengthSeconds_ <= 0.0)
        return 0.0f;
    const auto x = static_cast<float>((seconds - scrollSeconds_) * pixelsPerSecond_);
    return std::min(x, static_cast<float>(width_ - 1));
}

double WaveformView::pixelToSeconds(float x) const {
    if (width_ <= 0 || lengthSeconds_ <= 0.0 || pixelsPerSecond_ <= 0.0)
        return 0.0;
    return limit(0.0, lengthSeconds_, scrollSeconds_ + static_cast<double>(x) / pixelsPerSecond_);
}

WaveformView::Drag WaveformView::hitTest(int x, int y) const {
    if (!hasSource())
        return Drag::None;
    const auto mx = static_cast<float>(x);
    const auto near = [&](double seconds) {
        return std::abs(mx - secondsToPixel(seconds)) <= kMarkerHitPixels;
    };

    const bool loopFirst = markers_.loop && loopMarkersFirst_;
    if (loopFirst && near(markers_.loopEnd))
        return Drag::LoopEnd;
    if (loopFirst && near(markers_.loopStart))
        return Drag::LoopStart;
    if (near(markers_.end))
        return Drag::SampleEnd;
    if (near(markers_.start))
        return Drag::SampleStart;
    if (markers_.loop) {
        if (near(markers_.loopStart))
            return Drag::LoopStart;
        if (near(markers_.loopEnd))
            return Drag::LoopEnd;
        const float startX = secondsToPixel(markers_.loopStart);
        const float endX = secondsToPixel(markers_.loopEnd);
        if (endX > startX && mx >= startX && mx <= endX && y >= waveTop() &&
            y < waveTop() + kLoopBarHeight)
            return Drag::LoopRegion;
    }
    return Drag::None;
}

void WaveformView::setMarker(Drag drag, double seconds) {
    switch (drag) {
        case Drag::SampleStart:
            markers_.start = seconds;
            break;
        case Drag::SampleEnd:
            markers_.end = seconds;
            break;
        case Drag::LoopStart:
            markers_.loopStart = seconds;
            break;
        case Drag::LoopEnd:
            markers_.loopEnd = seconds;
            break;
        default:
            break;
    }
}

WaveformResponse WaveformView::pointerDown(const WaveformPointer& pointer) {
    WaveformResponse response;
    if (!hasSource())
        return response;
    const int x = toPixel(pointer.x);
    const int y = toPixel(pointer.y);
    downX_ = x;
    downY_ = y;
    zoomDirection_ = 0;

    if (inScrollBar(y)) {
        drag_ = scrollBarHitTest(x);
        if (drag_ == Drag::None) {
            // A press on the track centres the thumb there, then drags it.
            const double visible = static_cast<double>(width_) / pixelsPerSecond_;
            scrollSeconds_ = static_cast<double>(x) * lengthSeconds_ / width_ - visible * 0.5;
            clampScroll();
            drag_ = Drag::Thumb;
        }
        dragStartScroll_ = scrollSeconds_;
        dragStartPixelsPerSecond_ = pixelsPerSecond_;
        response.repaint = true;
        return response;
    }

    if (pointer.alt || pointer.middle) {
        drag_ = Drag::Scroll;
        dragStartScroll_ = scrollSeconds_;
        return response;
    }

    // Cmd-drag, or any drag on the ruler, zooms: up is in, down is out, about the press.
    if (pointer.command || overRuler(pointer)) {
        drag_ = Drag::Zoom;
        dragStartPixelsPerSecond_ = pixelsPerSecond_;
        zoomAnchorSeconds_ = pixelToSeconds(static_cast<float>(x));
        zoomAnchorX_ = x;
        return response;
    }

    drag_ = hitTest(x, y);
    if (drag_ == Drag::LoopRegion) {
        loopDragStart_ = markers_.loopStart;
        loopDragEnd_ = markers_.loopEnd;
        return response;
    }
    // Shift-click places the loop start.
    if (drag_ == Drag::None && pointer.shift)
        drag_ = Drag::LoopStart;
    if (drag_ == Drag::None)
        return response;

    setMarker(drag_, pixelToSeconds(static_cast<float>(x)));
    response.markersChanged = true;
    response.repaint = true;
    return response;
}

WaveformResponse WaveformView::pointerDrag(const WaveformPointer& pointer) {
    WaveformResponse response;
    if (drag_ == Drag::None || !hasSource())
        return response;
    const int x = toPixel(pointer.x);
    const int y = toPixel(pointer.y);
    response.repaint = true;

    switch (drag_) {
        case Drag::Scroll:
            scrollSeconds_ = dragStartScroll_ - static_cast<double>(x - downX_) / pixelsPerSecond_;
            clampScroll();
            break;

        case Drag::Zoom: {
            const int deltaY = downY_ - y;
            zoomDirection_ = deltaY > 0 ? 1 : (deltaY < 0 ? -1 : 0);
            // Log-scale zoom whose sensitivity grows with the zoom and eases off on long drags.
            const double minPps = minPixelsPerSecond();
            const double range = std::log(kMaxPixelsPerSecond) - std::log(minPps);
            const double position =
                (std::log(dragStartPixelsPerSecond_) - std::log(minPps)) / range;
            double sensitivity = 20.0 + position * 10.0;
            const double distance = std::abs(static_cast<double>(deltaY));
            if (distance > 80.0)
                sensitivity /= 1.0 + (distance - 80.0) / 150.0;
            pixelsPerSecond_ =
                limit(minPps, kMaxPixelsPerSecond,
                      dragStartPixelsPerSecond_ * std::pow(2.0, deltaY / sensitivity));
            scrollSeconds_ =
                zoomAnchorSeconds_ - static_cast<double>(zoomAnchorX_) / pixelsPerSecond_;
            clampScroll();
            break;
        }

        case Drag::Thumb:
        case Drag::ThumbStart:
        case Drag::ThumbEnd:
            dragThumb(drag_, x);
            break;

        case Drag::LoopRegion: {
            const double length = loopDragEnd_ - loopDragStart_;
            double start =
                std::max(0.0, loopDragStart_ + static_cast<double>(x - downX_) / pixelsPerSecond_);
            if (start + length > lengthSeconds_)
                start = lengthSeconds_ - length;
            markers_.loopStart = start;
            markers_.loopEnd = start + length;
            response.markersChanged = true;
            break;
        }

        default:
            setMarker(drag_, pixelToSeconds(static_cast<float>(x)));
            response.markersChanged = true;
            break;
    }
    return response;
}

WaveformResponse WaveformView::pointerUp(const WaveformPointer&) {
    WaveformResponse response;
    // The thumb draws brighter while held.
    response.repaint = drag_ == Drag::Thumb || drag_ == Drag::ThumbStart || drag_ == Drag::ThumbEnd;
    drag_ = Drag::None;
    zoomDirection_ = 0;
    return response;
}

WaveformResponse WaveformView::zoomBy(double factor, float anchorX) {
    WaveformResponse response;
    if (!hasSource())
        return response;
    const int x = toPixel(anchorX);
    const double anchor = pixelToSeconds(static_cast<float>(x));
    pixelsPerSecond_ = limit(minPixelsPerSecond(), kMaxPixelsPerSecond, pixelsPerSecond_ * factor);
    scrollSeconds_ = anchor - static_cast<double>(x) / pixelsPerSecond_;
    clampScroll();
    response.repaint = true;
    return response;
}

WaveformCursor WaveformView::cursor(const WaveformPointer& pointer) const {
    if (drag_ == Drag::Zoom)
        return zoomDirection_ > 0   ? WaveformCursor::ZoomIn
               : zoomDirection_ < 0 ? WaveformCursor::ZoomOut
                                    : WaveformCursor::Zoom;
    if (!hasSource())
        return WaveformCursor::Normal;
    if (inScrollBar(toPixel(pointer.y))) {
        switch (scrollBarHitTest(toPixel(pointer.x))) {
            case Drag::ThumbStart:
            case Drag::ThumbEnd:
                return WaveformCursor::ResizeLeftRight;
            case Drag::Thumb:
                return WaveformCursor::DraggingHand;
            default:
                return WaveformCursor::Normal;
        }
    }
    if (pointer.command || overRuler(pointer))
        return WaveformCursor::Zoom;
    switch (hitTest(toPixel(pointer.x), toPixel(pointer.y))) {
        case Drag::SampleStart:
        case Drag::SampleEnd:
        case Drag::LoopStart:
        case Drag::LoopEnd:
            return WaveformCursor::ResizeLeftRight;
        case Drag::LoopRegion:
            return WaveformCursor::DraggingHand;
        default:
            return WaveformCursor::Normal;
    }
}

void WaveformView::render(display::DisplayList& out) const {
    out.reset(static_cast<float>(width_), static_cast<float>(height_));
    if (!hasSource() || width_ <= 0 || height_ <= 0)
        return;

    const auto w = static_cast<float>(width_);
    const auto h = static_cast<float>(height_);
    out.save();
    out.clipRect({0.0f, 0.0f, w, h});

    const int waveTopPixel = waveTop();
    const auto bandTop = static_cast<float>(waveTopPixel);
    const auto bandHeight = static_cast<float>(waveBottom() - waveTopPixel);

    // Mirrored absolute peak per pixel column, top edge left to right, bottom edge back.
    const int waveHeight = waveBottom() - waveTopPixel - 2 * kWaveInset;
    if (waveHeight > 0 && pixelsPerSecond_ > 0.0) {
        const auto numSamples = source_->numSamples();
        const double samplesPerSecond = static_cast<double>(numSamples) / lengthSeconds_;
        const float half = static_cast<float>(waveHeight) * 0.5f;
        const auto top = static_cast<float>(waveTopPixel + kWaveInset);

        std::vector<float> peaks(static_cast<std::size_t>(width_));
        for (int x = 0; x < width_; ++x) {
            const double t = scrollSeconds_ + static_cast<double>(x) / pixelsPerSecond_;
            const auto first = std::clamp<std::int64_t>(
                static_cast<std::int64_t>(static_cast<int>(t * samplesPerSecond)), 0, numSamples);
            const auto last =
                std::clamp<std::int64_t>(static_cast<std::int64_t>(static_cast<int>(
                                             (t + 1.0 / pixelsPerSecond_) * samplesPerSecond)),
                                         0, numSamples);
            peaks[static_cast<std::size_t>(x)] = source_->absolutePeak(first, last) * gain_;
        }

        display::Path wave;
        wave.moveTo(0.0f, top + half);
        for (int x = 0; x < width_; ++x)
            wave.lineTo(static_cast<float>(x),
                        top + half - peaks[static_cast<std::size_t>(x)] * half);
        for (int x = width_ - 1; x >= 0; --x)
            wave.lineTo(static_cast<float>(x),
                        top + half + peaks[static_cast<std::size_t>(x)] * half);
        wave.close();

        out.fillPath(wave, Colour::of(ColourRole::Waveform).withAlpha(0.3f));
        out.strokePath(std::move(wave), Colour::of(ColourRole::Waveform).withAlpha(0.7f), 0.5f);
    }

    const auto line = [&](double seconds, const Colour& colour) {
        out.fillRect({static_cast<float>(static_cast<int>(secondsToPixel(seconds))), bandTop, 1.0f,
                      bandHeight},
                     colour);
    };

    if (markers_.loop) {
        const float startX = secondsToPixel(markers_.loopStart);
        const float endX = secondsToPixel(markers_.loopEnd);
        if (endX > startX) {
            out.fillRect({startX, bandTop, endX - startX, bandHeight},
                         Colour::of(ColourRole::LoopRegion).withAlpha(0.15f));
            out.fillRect({startX, bandTop, endX - startX, static_cast<float>(kLoopBarHeight)},
                         Colour::of(ColourRole::LoopRegion).withAlpha(0.5f));
        }
    }

    line(markers_.start, Colour::of(ColourRole::MarkerStart));
    line(markers_.end, Colour::of(ColourRole::MarkerEnd));
    if (markers_.loop) {
        line(markers_.loopStart, Colour::of(ColourRole::LoopRegion));
        line(markers_.loopEnd, Colour::of(ColourRole::LoopRegion));
    }
    if (playhead_ > 0.0)
        line(playhead_, Colour::of(ColourRole::Playhead));

    if (chrome_.ruler)
        renderRuler(out);
    if (chrome_.scrollBar)
        renderScrollBar(out);
    out.restore();
}

void WaveformView::renderRuler(display::DisplayList& out) const {
    const auto w = static_cast<float>(width_);
    const auto bottom = static_cast<float>(waveTop());
    out.fillRect({0.0f, 0.0f, w, bottom}, Colour::of(ColourRole::Surface));
    out.fillRect({0.0f, bottom - 1.0f, w, 1.0f}, Colour::of(ColourRole::Border));

    const bool samples = chrome_.units == WaveformUnits::Samples;
    const double unitsPerSecond =
        samples ? static_cast<double>(source_->numSamples()) / lengthSeconds_ : 1.0;
    const double pixelsPerUnit = pixelsPerSecond_ / unitsPerSecond;
    const auto [step, divisions] = rulerStep(kRulerSpacing / pixelsPerUnit, samples);
    const double minor = step / divisions;
    const double scroll = scrollSeconds_ * unitsPerSecond;
    const auto tickColour = Colour::of(ColourRole::TextDim);
    for (auto k = static_cast<std::int64_t>(std::floor(scroll / minor));; ++k) {
        const double units = static_cast<double>(k) * minor;
        const auto x = std::floor(static_cast<float>((units - scroll) * pixelsPerUnit));
        if (x > w)
            break;
        if (x < 0.0f)
            continue;
        const bool major = k % divisions == 0;
        const float tick = major ? kMajorTick : kMinorTick;
        out.fillRect({x, bottom - 1.0f - tick, 1.0f, tick},
                     tickColour.withAlpha(major ? 0.8f : 0.4f));
        if (major)
            out.text(rulerLabel(units, step, samples),
                     {x + 3.0f, 1.0f, kRulerLabelWidth, bottom - 3.0f}, tickColour, 9.0f);
    }
}

void WaveformView::renderScrollBar(display::DisplayList& out) const {
    const auto top = static_cast<float>(waveBottom());
    const auto height = static_cast<float>(height_) - top;
    out.strokeRect({0.5f, top + 1.5f, static_cast<float>(width_) - 1.0f, height - 2.0f},
                   Colour::of(ColourRole::Border), 1.0f, 3.0f);
    const auto [left, width] = thumb();
    const bool held = drag_ == Drag::Thumb || drag_ == Drag::ThumbStart || drag_ == Drag::ThumbEnd;
    const display::Rect body{left + 1.0f, top + 2.0f, width - 2.0f, height - 4.0f};
    out.fillRect(body, Colour::of(ColourRole::Accent).withAlpha(held ? 0.8f : 0.6f), 3.0f);
    out.strokeRect(body, Colour::of(ColourRole::Accent), 1.0f, 3.0f);
}

}  // namespace magda::sdk
