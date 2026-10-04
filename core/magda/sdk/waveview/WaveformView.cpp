#include "magda/sdk/waveview/WaveformView.hpp"

#include <algorithm>
#include <cmath>

namespace magda::sdk {

namespace {

using display::Colour;
using display::ColourRole;

constexpr int kMarkerHitPixels = 5;
constexpr int kLoopBarHeight = 8;
constexpr int kWaveInset = 2;
constexpr int kFallbackWidth = 200;

/// juce::jlimit, which tolerates inverted bounds.
template <typename T> T limit(T lower, T upper, T value) {
    return value < lower ? lower : (upper < value ? upper : value);
}

int toPixel(float v) {
    return static_cast<int>(std::lround(v));
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
        if (endX > startX && mx >= startX && mx <= endX && y >= 0 && y < kLoopBarHeight)
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

    if (pointer.alt || pointer.middle) {
        drag_ = Drag::Scroll;
        dragStartScroll_ = scrollSeconds_;
        return response;
    }

    // Cmd-drag zooms: up is in, down is out, about the time under the press.
    if (pointer.command) {
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
    drag_ = Drag::None;
    zoomDirection_ = 0;
    return {};
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
    if (pointer.command)
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

    // Mirrored absolute peak per pixel column, top edge left to right, bottom edge back.
    const int waveHeight = height_ - 2 * kWaveInset;
    if (waveHeight > 0 && pixelsPerSecond_ > 0.0) {
        const auto numSamples = source_->numSamples();
        const double samplesPerSecond = static_cast<double>(numSamples) / lengthSeconds_;
        const float half = static_cast<float>(waveHeight) * 0.5f;
        const auto top = static_cast<float>(kWaveInset);

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
        out.fillRect({static_cast<float>(static_cast<int>(secondsToPixel(seconds))), 0.0f, 1.0f, h},
                     colour);
    };

    if (markers_.loop) {
        const float startX = secondsToPixel(markers_.loopStart);
        const float endX = secondsToPixel(markers_.loopEnd);
        if (endX > startX) {
            out.fillRect({startX, 0.0f, endX - startX, h},
                         Colour::of(ColourRole::LoopRegion).withAlpha(0.15f));
            out.fillRect({startX, 0.0f, endX - startX, static_cast<float>(kLoopBarHeight)},
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

    out.restore();
}

}  // namespace magda::sdk
