#include <algorithm>
#include <cmath>
#include <string>

#include "magda/sdk/curve/CurveMath.hpp"
#include "magda/sdk/curveedit/CurveEditor.hpp"

namespace magda::sdk {

namespace {

using display::Colour;
using display::ColourRole;
using display::DisplayList;
using display::Path;

constexpr std::uint32_t kPencilPreviewArgb = 0xAAFFFFFF;
constexpr std::uint32_t kWhiteArgb = 0xFFFFFFFF;
constexpr float kPointSize = 5.0f;
constexpr float kSelectedPointSize = 6.0f;
constexpr float kIndicatorSize = 5.0f;
constexpr float kTriggerRadius = 3.0f;
constexpr float kTooltipFontSize = 10.0f;
constexpr int kTooltipHeight = 14;
constexpr int kPointBoxHalf = 9;
constexpr int kHandleBoxHalf = 4;
constexpr float kHandleInset = 1.25f;
constexpr float kHandleSize = 9.0f - 2.0f * kHandleInset;

/// juce::jlimit, which tolerates inverted bounds.
template <typename T> T limit(T lower, T upper, T value) {
    return value < lower ? lower : (upper < value ? upper : value);
}

/// A border inside @p r, as juce::Graphics::drawRect draws it.
void drawRectInside(DisplayList& out, display::Rect r, float thickness, const Colour& colour) {
    const auto take = [](float& from, float amount) { return std::min(amount, from); };
    const float top = take(r.height, thickness);
    if (top > 0.0f)
        out.fillRect({r.x, r.y, r.width, top}, colour);
    r.y += top;
    r.height -= top;
    const float bottom = take(r.height, thickness);
    if (bottom > 0.0f)
        out.fillRect({r.x, r.bottom() - bottom, r.width, bottom}, colour);
    r.height -= bottom;
    const float left = take(r.width, thickness);
    if (left > 0.0f && r.height > 0.0f)
        out.fillRect({r.x, r.y, left, r.height}, colour);
    r.x += left;
    r.width -= left;
    const float right = take(r.width, thickness);
    if (right > 0.0f && r.height > 0.0f)
        out.fillRect({r.right() - right, r.y, right, r.height}, colour);
}

/// A path that remembers its pen, for runs that continue from it.
struct PenPath {
    Path path;
    float x = 0.0f;
    float y = 0.0f;

    void moveTo(float px, float py) {
        path.moveTo(px, py);
        x = px;
        y = py;
    }
    void lineTo(float px, float py) {
        path.lineTo(px, py);
        x = px;
        y = py;
    }
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float px, float py) {
        path.cubicTo(c1x, c1y, c2x, c2y, px, py);
        x = px;
        y = py;
    }
};

std::string valueLabel(double y) {
    return std::to_string(static_cast<int>(std::nearbyint(y * 100.0))) + "%";
}

}  // namespace

void CurveEditor::render(DisplayList& out) const {
    out.reset(static_cast<float>(width_), static_cast<float>(height_));
    out.fillRect({0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)},
                 Colour::of(ColourRole::Background));

    renderGrid(out);
    renderCurve(out);

    if (drawing_ && canvasMode_ == CanvasMode::Pencil) {
        for (std::size_t i = 1; i < pencilPath_.size(); ++i) {
            Path segment;
            segment.moveTo(static_cast<float>(pencilPath_[i - 1].x),
                           static_cast<float>(pencilPath_[i - 1].y));
            segment.lineTo(static_cast<float>(pencilPath_[i].x),
                           static_cast<float>(pencilPath_[i].y));
            out.strokePath(std::move(segment), Colour::literal(kPencilPreviewArgb), 2.0f);
        }
    }

    if (lassoActive_ && lasso_.width > 0 && lasso_.height > 0) {
        const display::Rect r{static_cast<float>(lasso_.x), static_cast<float>(lasso_.y),
                              static_cast<float>(lasso_.width), static_cast<float>(lasso_.height)};
        out.fillRect(r, Colour::of(ColourRole::Curve).withAlpha(0.15f));
        drawRectInside(out, r, 1.0f, Colour::of(ColourRole::Curve).withAlpha(0.6f));
    }

    if (drawContentBorder_) {
        const auto c = content();
        drawRectInside(out,
                       {static_cast<float>(c.x), static_cast<float>(c.y),
                        static_cast<float>(c.width), static_cast<float>(c.height)},
                       1.0f, Colour::of(ColourRole::Guide));
    }

    renderIndicator(out);
    renderPoints(out);
    renderTooltip(out);
}

void CurveEditor::renderGrid(DisplayList& out) const {
    const auto width = static_cast<float>(width_);
    const auto height = static_cast<float>(height_);
    const auto line = [](bool centre) {
        return Colour::of(ColourRole::TextBright).withAlpha((centre ? 0x20 : 0x10) / 255.0f);
    };

    for (int i = 1; i < gridY_; ++i) {
        const double value = 1.0 - static_cast<double>(i) / gridY_;
        const auto y = static_cast<float>(static_cast<int>(yToPixelF(value)));
        out.fillRect({0.0f, y, width, 1.0f}, line(i * 2 == gridY_));
    }
    for (int i = 1; i < gridX_; ++i) {
        const auto x =
            static_cast<float>(static_cast<int>(xToPixelF(static_cast<double>(i) / gridX_)));
        out.fillRect({x, 0.0f, 1.0f, height}, line(i * 2 == gridX_));
    }

    if (!loopVisible_)
        return;

    const auto c = content();
    const auto top = static_cast<float>(c.y);
    const auto contentHeight = static_cast<float>(c.height);
    const auto left = static_cast<float>(c.x);
    const auto right = static_cast<float>(c.x + c.width);
    const auto startX = static_cast<float>(xToPixelF(loopStart_));
    const auto endX = static_cast<float>(xToPixelF(loopEnd_));

    const auto shade = Colour::of(ColourRole::Shade).withAlpha(0x30 / 255.0f);
    if (startX > left)
        out.fillRect({left, top, startX - left, contentHeight}, shade);
    if (endX < right)
        out.fillRect({endX, top, right - endX, contentHeight}, shade);

    const auto marker = Colour::of(ColourRole::Curve).withAlpha(0.7f);
    out.fillRect({static_cast<float>(static_cast<int>(startX)), top, 1.0f, contentHeight}, marker);
    out.fillRect({static_cast<float>(static_cast<int>(endX)), top, 1.0f, contentHeight}, marker);

    constexpr float kMarkerSize = 6.0f;
    Path start;
    start.moveTo(startX, top).lineTo(startX + kMarkerSize, top).lineTo(startX, top + kMarkerSize);
    start.close();
    out.fillPath(std::move(start), marker);
    Path end;
    end.moveTo(endX, top).lineTo(endX - kMarkerSize, top).lineTo(endX, top + kMarkerSize).close();
    out.fillPath(std::move(end), marker);
}

void CurveEditor::renderCurve(DisplayList& out) const {
    const auto ordered = renderOrder();
    if (ordered.empty())
        return;

    const auto px = [this](double x) { return static_cast<float>(xToPixelF(x)); };
    const auto py = [this](double y) { return static_cast<float>(yToPixelF(y)); };

    PenPath pen;
    const auto [firstX, firstY] = effectivePosition(*ordered.front());
    pen.moveTo(px(firstX), py(firstY));
    const float startX = pen.x;

    const auto ppx = static_cast<float>(usableWidth() > 0.0 ? usableWidth() : 100.0);
    const auto ppy = static_cast<float>(usableHeight() > 0.0 ? usableHeight() : 100.0);

    for (std::size_t i = 1; i < ordered.size(); ++i) {
        const auto& p1 = *ordered[i - 1];
        const auto& p2 = *ordered[i];
        const auto [x1, y1] = effectivePosition(p1);
        const auto [x2, y2] = effectivePosition(p2);
        const float pixelX1 = px(x1);
        const float pixelX2 = px(x2);
        const float pixelY2 = py(y2);

        switch (p1.interpolation) {
            case CurveInterpolation::Linear: {
                const bool stored = hasStoredShaper(p1, p2) || shaperPreviewId_ == p1.id;
                if (std::abs(p1.tension) < 0.001 && !stored) {
                    pen.lineTo(pixelX2, pixelY2);
                    break;
                }
                // Sampled from the engine's own segment evaluator, so the drawing is the output.
                const auto cy = shaperPosition(p1, p2).second;
                const int samples =
                    limit(64, 256, static_cast<int>(std::abs(pixelX2 - pixelX1) * 0.5f));
                for (int s = 1; s <= samples; ++s) {
                    const double t = static_cast<double>(s) / samples;
                    const double y = curvemath::evalSegment(
                        static_cast<float>(y1), static_cast<float>(y2), static_cast<float>(cy),
                        static_cast<float>(p1.tension), stored, static_cast<float>(t));
                    pen.lineTo(px(x1 + (x2 - x1) * t), py(y));
                }
                break;
            }
            case CurveInterpolation::Bezier: {
                const float pixelY1 = py(y1);
                pen.cubicTo(pixelX1 + static_cast<float>(p1.outHandleX * ppx),
                            pixelY1 - static_cast<float>(p1.outHandleY * ppy),
                            pixelX2 + static_cast<float>(p2.inHandleX * ppx),
                            pixelY2 - static_cast<float>(p2.inHandleY * ppy), pixelX2, pixelY2);
                break;
            }
            case CurveInterpolation::Step:
                pen.lineTo(pixelX2, pen.y);
                pen.lineTo(pixelX2, pixelY2);
                break;
            case CurveInterpolation::HardCorner: {
                const auto [apexX, apexY] = shaperPosition(p1, p2);
                pen.lineTo(px(apexX), py(apexY));
                pen.lineTo(pixelX2, pixelY2);
                break;
            }
        }
    }

    const float strokeWidth = std::clamp(static_cast<float>(height_) / 80.0f, 2.0f, 4.5f);
    Path fill = pen.path;
    const float baseY = py(0.0);
    fill.lineTo(pen.x, baseY).lineTo(startX, baseY).close();

    out.strokePath(std::move(pen.path), Colour::of(ColourRole::Curve), strokeWidth,
                   display::LineJoin::Round, display::LineCap::Round);
    out.fillPath(std::move(fill), Colour::of(ColourRole::Curve).withAlpha(0.13f));
}

void CurveEditor::renderIndicator(DisplayList& out) const {
    if (!indicatorVisible_)
        return;

    const auto c = content();
    const int x = static_cast<int>(xToPixelF(indicatorPhase_));
    const int y = static_cast<int>(yToPixelF(indicatorValue_));
    const auto fx = static_cast<float>(x);
    const auto fy = static_cast<float>(y);

    if (showCrosshair_) {
        const auto cross = Colour::of(ColourRole::Curve).withAlpha(0.4f);
        out.fillRect({fx, static_cast<float>(c.y), 1.0f, static_cast<float>(c.height)}, cross);
        out.fillRect({static_cast<float>(c.x), fy, static_cast<float>(c.width), 1.0f}, cross);
    }

    const display::Rect dot{fx - kIndicatorSize / 2.0f, fy - kIndicatorSize / 2.0f, kIndicatorSize,
                            kIndicatorSize};
    out.fillEllipse(dot, Colour::of(ColourRole::Curve));
    out.strokeEllipse(dot, Colour::literal(kWhiteArgb), 1.0f);

    // In the top-right of the padding frame, clear of a point parked at (1, 1).
    const display::Rect trigger{static_cast<float>(width_) - kTriggerRadius * 2.0f - 3.0f, 3.0f,
                                kTriggerRadius * 2.0f, kTriggerRadius * 2.0f};
    if (triggerLit_)
        out.fillEllipse(trigger, Colour::of(ColourRole::Curve));
    else
        out.strokeEllipse(trigger, Colour::of(ColourRole::Curve).withAlpha(0.3f), 1.0f);
}

void CurveEditor::renderPoints(DisplayList& out) const {
    const float scale = pointScale();

    for (const auto& p : points_) {
        const auto [ex, ey] = effectivePosition(p);
        const auto cx = static_cast<float>(xToPixel(ex));
        const auto cy = static_cast<float>(yToPixel(ey));
        const bool selected = selection_.contains(p.id);
        const bool hovered = hover_.target == Target::Point && hover_.id == p.id;
        const bool hard = p.interpolation == CurveInterpolation::HardCorner;

        const float radius = (selected ? kSelectedPointSize : kPointSize) * scale / 2.0f;
        const display::Rect body{cx - radius, cy - radius, radius * 2.0f, radius * 2.0f};
        auto fill = Colour::of(ColourRole::CurvePoint);
        if (hovered)
            fill = fill.brightened(0.25f);
        if (hard)
            out.fillRect(body, fill);
        else
            out.fillEllipse(body, fill);

        if (selected) {
            const float ringRadius = radius + 2.0f;
            const display::Rect ring{cx - ringRadius, cy - ringRadius, ringRadius * 2.0f,
                                     ringRadius * 2.0f};
            const auto ringColour = Colour::of(ColourRole::TextBright).withAlpha(0.9f);
            if (hard)
                drawRectInside(out, ring, 1.5f, ringColour);
            else
                out.strokeEllipse(ring, ringColour, 1.5f);
        }
    }

    for (const auto& [id, pixel] : visibleHandles()) {
        const auto* owner = find(id);
        const bool dragging = gesture_ == Target::Handle && gestureId_ == id && !popupGesture_;
        const bool hovered = hover_.target == Target::Handle && hover_.id == id;
        const display::Rect body{static_cast<float>(pixel.x - kHandleBoxHalf) + kHandleInset,
                                 static_cast<float>(pixel.y - kHandleBoxHalf) + kHandleInset,
                                 kHandleSize, kHandleSize};
        const float lineWidth = dragging || hovered ? 1.4f : 1.1f;
        const auto stroke = dragging  ? Colour::of(ColourRole::CurvePoint)
                            : hovered ? Colour::of(ColourRole::TextBright)
                                      : Colour::of(ColourRole::HandleStroke);

        if (owner != nullptr && owner->interpolation == CurveInterpolation::HardCorner) {
            out.fillRect(body, Colour::of(ColourRole::Handle), 0.5f);
            out.strokeRect(body, stroke, lineWidth, 0.5f);
        } else {
            out.fillEllipse(body, Colour::of(ColourRole::Handle));
            out.strokeEllipse(body, stroke, lineWidth);
        }
    }
}

void CurveEditor::renderTooltip(DisplayList& out) const {
    std::uint32_t id = 0;
    if (previewLead_)
        id = *previewLead_;
    else if (hover_.target == Target::Point)
        id = hover_.id;
    const auto* p = find(id);
    if (p == nullptr)
        return;

    const auto [ex, ey] = effectivePosition(*p);
    const double value = previewLead_ ? previewPositions_.at(*previewLead_).second : p->y;
    const auto label = valueLabel(value);

    const float measured = measure_ ? measure_(label, kTooltipFontSize)
                                    : 0.6f * kTooltipFontSize * static_cast<float>(label.size());
    const int textWidth = static_cast<int>(measured) + 6;
    const int cx = xToPixel(ex);
    const int cy = yToPixel(ey);

    int tx = limit(0, width_ - textWidth, cx - textWidth / 2);
    int ty = cy - kPointBoxHalf - kTooltipHeight - 2;
    if (ty < 0)
        ty = cy + kPointBoxHalf + 2;

    const display::Rect box{static_cast<float>(tx), static_cast<float>(ty),
                            static_cast<float>(textWidth), static_cast<float>(kTooltipHeight)};
    out.fillRect(box, Colour::of(ColourRole::Tooltip), 3.0f);
    out.text(label, box, Colour::of(ColourRole::TooltipText), kTooltipFontSize,
             display::Justification::Centre);
}

}  // namespace magda::sdk
