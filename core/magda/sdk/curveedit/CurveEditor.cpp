#include "magda/sdk/curveedit/CurveEditor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "magda/sdk/curve/CurveMath.hpp"

namespace magda::sdk {

namespace {

/// Keeps edge points grabbable: half the point hit box.
constexpr int kEdgePadding = 8;
constexpr int kPointBox = 18;
constexpr int kHandleBox = 9;
constexpr int kMinHandleSegmentPixels = 30;
constexpr int kLassoThresholdSquared = 16;
constexpr int kMoveCommitPixels = 2;
constexpr int kPencilSpacing = 10;
constexpr int kLoopStrip = 12;
constexpr int kLoopHitTolerance = 6;
constexpr float kLoopMinGap = 0.02f;
constexpr double kEdgePinEpsilon = 0.001;
constexpr double kStampEpsilon = 1e-6;
constexpr double kShaperEpsilon = 1e-6;
/// A Linear bend may push its control this far past the endpoints' range.
constexpr double kBendOvershoot = 8.0;

CurveInterpolation interpolationOf(int curveType) {
    switch (curveType) {
        case 1:
            return CurveInterpolation::Bezier;
        case 2:
            return CurveInterpolation::Step;
        case 3:
            return CurveInterpolation::HardCorner;
        default:
            return CurveInterpolation::Linear;
    }
}

CurveEditorPoint fromData(const CurvePointData& d, std::uint32_t id) {
    CurveEditorPoint p;
    p.id = id;
    p.x = static_cast<double>(d.phase);
    p.y = static_cast<double>(d.value);
    p.interpolation = interpolationOf(d.curveType);
    p.tension = static_cast<double>(d.tension);
    p.inHandleX = static_cast<double>(d.inHandleX);
    p.inHandleY = static_cast<double>(d.inHandleY);
    p.outHandleX = static_cast<double>(d.outHandleX);
    p.outHandleY = static_cast<double>(d.outHandleY);
    return p;
}

void copyValues(CurveEditorPoint& p, const CurvePointData& d) {
    p = fromData(d, p.id);
}

CurvePointData toData(const CurveEditorPoint& p) {
    CurvePointData d;
    d.phase = static_cast<float>(p.x);
    d.value = static_cast<float>(p.y);
    d.tension = static_cast<float>(p.tension);
    d.curveType = static_cast<int>(p.interpolation);
    d.inHandleX = static_cast<float>(p.inHandleX);
    d.inHandleY = static_cast<float>(p.inHandleY);
    d.outHandleX = static_cast<float>(p.outHandleX);
    d.outHandleY = static_cast<float>(p.outHandleY);
    return d;
}

bool bendable(const CurveEditorPoint& p) {
    return p.interpolation == CurveInterpolation::Linear ||
           p.interpolation == CurveInterpolation::HardCorner;
}

/// juce::jlimit, which tolerates inverted bounds.
template <typename T> T limit(T lower, T upper, T value) {
    return value < lower ? lower : (upper < value ? upper : value);
}

int roundToPixel(float v) {
    return static_cast<int>(std::lround(v));
}

}  // namespace

CurveEditor::CurveEditor() = default;

// Model

void CurveEditor::setPoints(std::span<const CurvePointData> points) {
    gesture_ = Target::None;
    drawing_ = false;
    lassoActive_ = false;
    clearPreview();
    selection_.clear();
    hover_ = {};

    points_.clear();
    nextId_ = 1;
    for (const auto& d : points)
        points_.push_back(fromData(d, nextId_++));
    sortPoints();
    if (!points_.empty()) {
        points_.front().x = 0.0;
        points_.back().x = 1.0;
    }
}

void CurveEditor::refreshPoints(std::span<const CurvePointData> points) {
    if (points_.empty() || points.size() != points_.size()) {
        setPoints(points);
        return;
    }
    for (std::size_t i = 0; i < points.size(); ++i)
        copyValues(points_[i], points[i]);
    points_.front().x = 0.0;
    points_.back().x = 1.0;
}

void CurveEditor::syncPoints(std::span<const CurvePointData> points) {
    for (std::size_t i = 0; i < points_.size() && i < points.size(); ++i)
        copyValues(points_[i], points[i]);
}

std::vector<CurvePointData> CurveEditor::points() const {
    std::vector<CurvePointData> out;
    out.reserve(points_.size());
    for (const auto& p : points_)
        out.push_back(toData(p));
    return out;
}

std::vector<CurvePointData> CurveEditor::effectivePoints() const {
    std::vector<CurvePointData> out;
    out.reserve(points_.size());
    for (std::size_t i = 0; i < points_.size(); ++i) {
        auto p = points_[i];
        std::tie(p.x, p.y) = effectivePosition(p);
        if (shaperPreviewId_ == p.id) {
            p.outHandleX = shaperPreviewX_ - points_[i].x;
            p.outHandleY = shaperPreviewY_ - points_[i].y;
        } else if (i > 0 && shaperPreviewId_ == points_[i - 1].id) {
            p.inHandleX = shaperPreviewX_ - points_[i].x;
            p.inHandleY = shaperPreviewY_ - points_[i].y;
        }
        out.push_back(toData(p));
    }
    return out;
}

// Surface

void CurveEditor::setSize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
}

void CurveEditor::setPadding(int padding) {
    padding_ = std::max(0, padding);
}

void CurveEditor::setGrid(int divisionsX, int divisionsY) {
    gridX_ = std::max(1, divisionsX);
    gridY_ = std::max(1, divisionsY);
}

void CurveEditor::setSnap(bool x, bool y, bool loop) {
    snapX_ = x;
    snapY_ = y;
    snapLoop_ = loop;
}

void CurveEditor::setLoopRegion(bool visible, float start, float end) {
    loopVisible_ = visible;
    if (gesture_ != Target::LoopMarker) {
        loopStart_ = start;
        loopEnd_ = end;
    }
}

void CurveEditor::setIndicator(float phase, float value, bool triggerLit) {
    indicatorVisible_ = true;
    indicatorPhase_ = phase;
    indicatorValue_ = value;
    triggerLit_ = triggerLit;
}

// Mapping

CurveEditor::Rect CurveEditor::content() const {
    return {padding_, padding_, std::max(0, width_ - 2 * padding_),
            std::max(0, height_ - 2 * padding_)};
}

double CurveEditor::usableWidth() const {
    return content().width - 2.0 * kEdgePadding;
}

double CurveEditor::usableHeight() const {
    return content().height - 2.0 * kEdgePadding;
}

double CurveEditor::pixelToX(int px) const {
    const double usable = usableWidth();
    if (usable <= 0.0)
        return 0.0;
    return static_cast<double>(px - content().x - kEdgePadding) / usable;
}

double CurveEditor::pixelToY(int py) const {
    const double usable = usableHeight();
    if (usable <= 0.0)
        return 0.5;
    return 1.0 - static_cast<double>(py - content().y - kEdgePadding) / usable;
}

int CurveEditor::xToPixel(double x) const {
    return content().x + kEdgePadding + static_cast<int>(x * usableWidth());
}

int CurveEditor::yToPixel(double y) const {
    return content().y + kEdgePadding + static_cast<int>((1.0 - y) * usableHeight());
}

double CurveEditor::xToPixelF(double x) const {
    return static_cast<double>(content().x + kEdgePadding) + x * usableWidth();
}

double CurveEditor::yToPixelF(double y) const {
    return static_cast<double>(content().y + kEdgePadding) + (1.0 - y) * usableHeight();
}

float CurveEditor::pointScale() const {
    return std::clamp(static_cast<float>(height_) / 170.0f, 1.0f, 1.7f);
}

// Geometry

const CurveEditorPoint* CurveEditor::find(std::uint32_t id) const {
    const auto it = std::ranges::find(points_, id, &CurveEditorPoint::id);
    return it == points_.end() ? nullptr : &*it;
}

CurveEditorPoint* CurveEditor::find(std::uint32_t id) {
    const auto it = std::ranges::find(points_, id, &CurveEditorPoint::id);
    return it == points_.end() ? nullptr : &*it;
}

std::pair<double, double> CurveEditor::effectivePosition(const CurveEditorPoint& p) const {
    if (previewLead_) {
        const auto it = previewPositions_.find(p.id);
        if (it != previewPositions_.end())
            return it->second;
    }
    return {p.x, p.y};
}

std::vector<const CurveEditorPoint*> CurveEditor::renderOrder() const {
    std::vector<const CurveEditorPoint*> ordered;
    ordered.reserve(points_.size());
    for (const auto& p : points_)
        ordered.push_back(&p);
    // A dragged point may cross its neighbours; the commit re-sorts, so the preview does too.
    if (previewLead_)
        std::ranges::stable_sort(
            ordered, {}, [this](const CurveEditorPoint* p) { return effectivePosition(*p).first; });
    return ordered;
}

bool CurveEditor::hasStoredShaper(const CurveEditorPoint& p1, const CurveEditorPoint& p2) const {
    return std::abs(p1.outHandleX) > kShaperEpsilon || std::abs(p1.outHandleY) > kShaperEpsilon ||
           std::abs(p2.inHandleX) > kShaperEpsilon || std::abs(p2.inHandleY) > kShaperEpsilon;
}

std::pair<double, double> CurveEditor::shaperPosition(const CurveEditorPoint& p1,
                                                      const CurveEditorPoint& p2) const {
    if (shaperPreviewId_ == p1.id)
        return {shaperPreviewX_, shaperPreviewY_};

    const auto [p1x, p1y] = effectivePosition(p1);
    const auto [p2x, p2y] = effectivePosition(p2);

    if (hasStoredShaper(p1, p2)) {
        const double sx = limit(p1x, p2x, p1x + p1.outHandleX);
        const double sy = p1y + p1.outHandleY;
        // A hard corner's apex is on the curve, so it stays in range; a Linear control may not.
        if (p1.interpolation == CurveInterpolation::HardCorner)
            return {sx, std::clamp(sy, 0.0, 1.0)};
        return {sx, sy};
    }

    double sy = (p1y + p2y) * 0.5;
    if (std::abs(p1.tension) > 0.001) {
        const double curvedT = p1.tension > 0 ? std::pow(0.5, 1.0 + p1.tension * 2.0)
                                              : 1.0 - std::pow(0.5, 1.0 - p1.tension * 2.0);
        sy = p1y + curvedT * (p2y - p1y);
    }
    return {(p1x + p2x) * 0.5, std::clamp(sy, 0.0, 1.0)};
}

std::pair<double, double> CurveEditor::handlePosition(const CurveEditorPoint& p1,
                                                      const CurveEditorPoint& p2) const {
    const auto [p1x, p1y] = effectivePosition(p1);
    const auto [p2x, p2y] = effectivePosition(p2);
    const auto [cx, cy] = shaperPosition(p1, p2);
    if (p1.interpolation == CurveInterpolation::HardCorner)
        return {cx, cy};
    const bool stored = hasStoredShaper(p1, p2) || shaperPreviewId_ == p1.id;
    const double midY = curvemath::evalSegment(static_cast<float>(p1y), static_cast<float>(p2y),
                                               static_cast<float>(cy),
                                               static_cast<float>(p1.tension), stored, 0.5f);
    return {0.5 * (p1x + p2x), midY};
}

std::vector<std::pair<std::uint32_t, CurveEditor::Pixel>> CurveEditor::visibleHandles() const {
    std::vector<std::pair<std::uint32_t, Pixel>> handles;
    if (points_.size() < 2)
        return handles;

    // A segment handle belongs to the left point of a bendable segment in model order.
    std::vector<std::uint32_t> owners;
    for (std::size_t i = 0; i + 1 < points_.size(); ++i)
        if (bendable(points_[i]))
            owners.push_back(points_[i].id);

    const auto ordered = renderOrder();
    std::map<std::uint32_t, Pixel> placed;
    for (std::size_t i = 0; i + 1 < ordered.size(); ++i) {
        const auto& p1 = *ordered[i];
        const auto& p2 = *ordered[i + 1];
        if (!bendable(p1) || std::ranges::find(owners, p1.id) == owners.end())
            continue;
        const auto [x1, y1] = effectivePosition(p1);
        const auto [x2, y2] = effectivePosition(p2);
        if (xToPixel(x2) - xToPixel(x1) < kMinHandleSegmentPixels)
            continue;
        const auto [mx, my] = handlePosition(p1, p2);
        placed[p1.id] = {xToPixel(mx), yToPixel(my)};
    }

    for (const auto id : owners)
        if (const auto it = placed.find(id); it != placed.end())
            handles.emplace_back(id, it->second);
    return handles;
}

int CurveEditor::loopMarkerAt(int px, int py) const {
    if (!loopVisible_)
        return 0;
    if (py > content().y + kLoopStrip)
        return 0;
    const int startX = static_cast<int>(std::round(xToPixelF(loopStart_)));
    const int endX = static_cast<int>(std::round(xToPixelF(loopEnd_)));
    const int dStart = std::abs(px - startX);
    const int dEnd = std::abs(px - endX);
    if (dStart <= kLoopHitTolerance && dStart <= dEnd)
        return 1;
    if (dEnd <= kLoopHitTolerance)
        return 2;
    return 0;
}

CurveEditor::Hit CurveEditor::hitTest(int px, int py) const {
    // Topmost first: segment handles sit above points, later points above earlier ones.
    const auto handles = visibleHandles();
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
        const int left = it->second.x - kHandleBox / 2;
        const int top = it->second.y - kHandleBox / 2;
        if (px >= left && px < left + kHandleBox && py >= top && py < top + kHandleBox)
            return {Target::Handle, it->first, 0};
    }

    const float hitRadius = std::max(6.0f * pointScale() / 2.0f + 4.0f, 7.0f);
    for (auto it = points_.rbegin(); it != points_.rend(); ++it) {
        const auto [ex, ey] = effectivePosition(*it);
        const int cx = xToPixel(ex);
        const int cy = yToPixel(ey);
        const int dx = px - cx;
        const int dy = py - cy;
        if (dx < -kPointBox / 2 || dx >= kPointBox / 2 || dy < -kPointBox / 2 ||
            dy >= kPointBox / 2)
            continue;
        if (std::sqrt(static_cast<float>(dx * dx + dy * dy)) <= hitRadius)
            return {Target::Point, it->id, 0};
    }

    if (const int marker = loopMarkerAt(px, py))
        return {Target::LoopMarker, 0, marker};
    return {Target::Canvas, 0, 0};
}

std::uint32_t CurveEditor::segmentOwnerAt(double x) const {
    for (std::size_t i = 0; i + 1 < points_.size(); ++i)
        if (x >= points_[i].x && x <= points_[i + 1].x)
            return points_[i].id;
    if (points_.size() >= 2)
        return points_[points_.size() - 2].id;
    return 0;
}

// Snapping

double CurveEditor::snapX(double x) const {
    if (!snapX_ || gridX_ <= 1)
        return x;
    const double step = 1.0 / gridX_;
    return std::clamp(std::round(x / step) * step, 0.0, 1.0);
}

double CurveEditor::snapY(double y) const {
    if (!snapY_ || gridY_ <= 1)
        return y;
    const double step = 1.0 / gridY_;
    return std::clamp(std::round(y / step) * step, 0.0, 1.0);
}

void CurveEditor::constrain(std::uint32_t id, double& x, double& y) const {
    x = std::clamp(x, 0.0, 1.0);
    y = std::clamp(y, 0.0, 1.0);

    // The cycle's ends stay put; an edge point is one sitting at 0 or 1 now.
    bool edge = false;
    if (const auto* p = find(id)) {
        if (std::abs(p->x) < kEdgePinEpsilon) {
            x = 0.0;
            edge = true;
        } else if (std::abs(p->x - 1.0) < kEdgePinEpsilon) {
            x = 1.0;
            edge = true;
        }
    }
    if (!edge)
        x = snapX(x);
    y = snapY(y);
}

// Edits

void CurveEditor::sortPoints() {
    std::ranges::stable_sort(points_, {}, &CurveEditorPoint::x);
}

void CurveEditor::clearPreview() {
    previewLead_.reset();
    previewPositions_.clear();
    shaperPreviewId_.reset();
    multiStart_.clear();
}

void CurveEditor::beginEdit(CurveEditorResponse& response) const {
    response.before = points();
}

void CurveEditor::endEdit(CurveEditorResponse& response, CurveEditorResponse::Commit kind) const {
    response.commit = kind;
    response.repaint = true;
}

void CurveEditor::pointPreview(std::uint32_t id, double x, double y) {
    x = snapX(x);
    y = snapY(y);
    constrain(id, x, y);

    if (previewLead_ != id) {
        multiStart_.clear();
        for (const auto& p : points_)
            if (selection_.contains(p.id))
                multiStart_[p.id] = {p.x, p.y};
    }

    previewLead_ = id;
    previewPositions_.clear();
    previewPositions_[id] = {x, y};

    if (selection_.size() > 1 && multiStart_.contains(id)) {
        const auto& lead = multiStart_.at(id);
        const double dx = x - lead.first;
        const double dy = y - lead.second;
        for (const auto& [pid, start] : multiStart_)
            if (pid != id)
                previewPositions_[pid] = {std::max(0.0, start.first + dx),
                                          std::clamp(start.second + dy, 0.0, 1.0)};
    }
}

void CurveEditor::commitPointMove(std::uint32_t id, double x, double y) {
    previewLead_.reset();
    previewPositions_.clear();

    x = snapX(x);
    y = snapY(y);
    constrain(id, x, y);

    std::map<std::uint32_t, std::pair<double, double>> finals;
    if (selection_.size() > 1 && multiStart_.contains(id)) {
        const auto& lead = multiStart_.at(id);
        const double dx = x - lead.first;
        const double dy = y - lead.second;
        for (const auto& [pid, start] : multiStart_) {
            if (pid == id) {
                finals[pid] = {x, y};
                continue;
            }
            double fx = snapX(std::max(0.0, start.first + dx));
            double fy = snapY(std::clamp(start.second + dy, 0.0, 1.0));
            constrain(pid, fx, fy);
            finals[pid] = {fx, fy};
        }
    } else {
        finals[id] = {x, y};
    }
    multiStart_.clear();

    for (auto& p : points_)
        if (const auto it = finals.find(p.id); it != finals.end())
            std::tie(p.x, p.y) = it->second;
    sortPoints();
}

std::optional<std::pair<double, double>> CurveEditor::shaperFromPixel(std::uint32_t id, double px,
                                                                      double py) const {
    for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
        if (points_[i].id != id)
            continue;
        const auto& p1 = points_[i];
        const auto& p2 = points_[i + 1];
        const int pixelX = static_cast<int>(std::round(px));
        const int pixelY = static_cast<int>(std::round(py));

        if (p1.interpolation == CurveInterpolation::HardCorner) {
            double cx = limit(p1.x, p2.x, pixelToX(pixelX));
            double cy = std::clamp(pixelToY(pixelY), 0.0, 1.0);
            cx = limit(p1.x, p2.x, snapX(cx));
            cy = std::clamp(snapY(cy), 0.0, 1.0);
            return std::pair{cx, cy};
        }

        // Linear bends vertically: the cursor is the on-curve midpoint, and the quadratic
        // control is solved from B(0.5) = 0.25 P1 + 0.5 C + 0.25 P2.
        const double lo = std::min(p1.y, p2.y) - kBendOvershoot;
        const double hi = std::max(p1.y, p2.y) + kBendOvershoot;
        double handleY = std::clamp(pixelToY(pixelY), lo, hi);
        handleY = std::clamp(snapY(handleY), lo, hi);
        return std::pair{0.5 * (p1.x + p2.x), 2.0 * handleY - 0.5 * (p1.y + p2.y)};
    }
    return std::nullopt;
}

void CurveEditor::applyShaper(std::uint32_t id, double cx, double cy) {
    for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
        if (points_[i].id != id)
            continue;
        auto& p1 = points_[i];
        auto& p2 = points_[i + 1];
        p1.outHandleX = cx - p1.x;
        p1.outHandleY = cy - p1.y;
        p2.inHandleX = cx - p2.x;
        p2.inHandleY = cy - p2.y;
        return;
    }
}

void CurveEditor::toggleHardCorner(std::uint32_t id) {
    clearPreview();
    for (std::size_t i = 0; i < points_.size(); ++i) {
        auto& p = points_[i];
        if (p.id != id)
            continue;
        if (p.interpolation == CurveInterpolation::HardCorner) {
            p.interpolation = CurveInterpolation::Linear;
            return;
        }
        // The corner starts where the smooth curve already passes, keeping the bend.
        if (i + 1 < points_.size()) {
            const auto [apexX, apexY] = handlePosition(p, points_[i + 1]);
            p.interpolation = CurveInterpolation::HardCorner;
            p.outHandleX = apexX - p.x;
            p.outHandleY = apexY - p.y;
            points_[i + 1].inHandleX = apexX - points_[i + 1].x;
            points_[i + 1].inHandleY = apexY - points_[i + 1].y;
        } else {
            p.interpolation = CurveInterpolation::HardCorner;
        }
        return;
    }
}

void CurveEditor::addPoint(double x, double y, CurveInterpolation interpolation) {
    CurveEditorPoint p;
    p.id = nextId_++;
    p.x = std::clamp(x, 0.0, 1.0);
    p.y = std::clamp(y, 0.0, 1.0);
    p.interpolation = interpolation;
    const auto at = std::ranges::lower_bound(points_, p.x, {}, &CurveEditorPoint::x);
    points_.insert(at, p);
}

bool CurveEditor::deletePoints(const std::set<std::uint32_t>& ids) {
    // Two points always remain; the selection is removed in model order up to that floor.
    const std::size_t deletable = points_.size() > 2 ? points_.size() - 2 : 0;
    std::size_t deleted = 0;
    std::erase_if(points_, [&](const CurveEditorPoint& p) {
        if (deleted >= deletable || !ids.contains(p.id))
            return false;
        ++deleted;
        return true;
    });
    return deleted > 0;
}

void CurveEditor::stampStep(double gridStart, double gridEnd, double y, std::uint32_t prevId,
                            double prevValue) {
    // A cell one grid step wide: the point before it turns Step so the cell opens with a
    // cliff, and a Linear point at the cell's end returns to that point's value.
    if (auto* prev = find(prevId))
        prev->interpolation = CurveInterpolation::Step;

    const auto at = std::ranges::find_if(
        points_, [&](const auto& p) { return std::abs(p.x - gridStart) < kStampEpsilon; });
    if (at != points_.end()) {
        at->y = std::clamp(y, 0.0, 1.0);
        at->interpolation = CurveInterpolation::Step;
    } else {
        addPoint(gridStart, y, CurveInterpolation::Step);
    }

    const bool hasEnd = std::ranges::any_of(
        points_, [&](const auto& p) { return std::abs(p.x - gridEnd) < kStampEpsilon; });
    if (gridEnd > gridStart + kStampEpsilon && !hasEnd)
        addPoint(gridEnd, prevValue, CurveInterpolation::Linear);
}

void CurveEditor::addPencilPoints() {
    if (pencilPath_.size() < 2)
        return;
    std::vector<Pixel> kept{pencilPath_.front()};
    for (std::size_t i = 1; i < pencilPath_.size(); ++i) {
        const int dx = pencilPath_[i].x - kept.back().x;
        const int dy = pencilPath_[i].y - kept.back().y;
        if (dx * dx + dy * dy >= kPencilSpacing * kPencilSpacing)
            kept.push_back(pencilPath_[i]);
    }
    if (kept.back().x != pencilPath_.back().x || kept.back().y != pencilPath_.back().y)
        kept.push_back(pencilPath_.back());

    for (const auto& pixel : kept)
        addPoint(snapX(pixelToX(pixel.x)), snapY(pixelToY(pixel.y)), CurveInterpolation::Linear);
}

// Input

CurveEditorResponse CurveEditor::canvasDown(const CurveEditorPointer& pointer, int px, int py) {
    CurveEditorResponse response;
    if (const int marker = loopMarkerAt(px, py)) {
        gesture_ = Target::LoopMarker;
        gestureMarker_ = marker;
        return response;
    }

    if (pointer.mods.popup) {
        gesture_ = Target::Canvas;
        popupGesture_ = true;
        const auto owner = segmentOwnerAt(pixelToX(px));
        if (find(owner) == nullptr)
            return response;
        beginEdit(response);
        toggleHardCorner(owner);
        endEdit(response, CurveEditorResponse::Commit::Edit);
        return response;
    }

    if (!pointer.primary)
        return response;

    gesture_ = Target::Canvas;
    popupGesture_ = false;
    canvasMode_ = pointer.mods.command ? CanvasMode::Pencil
                  : pointer.mods.shift ? CanvasMode::Stamp
                                       : CanvasMode::Select;

    switch (canvasMode_) {
        case CanvasMode::Select:
            anchor_ = {px, py};
            lassoActive_ = false;
            lasso_ = {};
            break;
        case CanvasMode::Pencil:
            drawing_ = true;
            pencilPath_.assign(1, {px, py});
            break;
        case CanvasMode::Stamp: {
            const double x = pixelToX(px);
            const double y = std::clamp(snapY(pixelToY(py)), 0.0, 1.0);
            const double gridStart = snapX(x);
            const double gridEnd = gridStart + (gridX_ > 0 ? 1.0 / gridX_ : 0.0);

            std::uint32_t prevId = 0;
            double prevValue = 0.5;
            double best = -std::numeric_limits<double>::infinity();
            for (const auto& p : points_) {
                if (p.x < gridStart && p.x > best) {
                    best = p.x;
                    prevId = p.id;
                    prevValue = p.y;
                }
            }
            beginEdit(response);
            stampStep(gridStart, gridEnd, y, prevId, prevValue);
            endEdit(response, CurveEditorResponse::Commit::StampStep);
            break;
        }
    }
    return response;
}

CurveEditorResponse CurveEditor::pointerDown(const CurveEditorPointer& pointer) {
    if (gesture_ != Target::None)
        return {};

    const int px = roundToPixel(pointer.x);
    const int py = roundToPixel(pointer.y);
    const auto hit = hitTest(px, py);
    gestureId_ = hit.id;
    popupGesture_ = false;
    hover_ = hit;

    CurveEditorResponse response;
    switch (hit.target) {
        case Target::Handle: {
            gesture_ = Target::Handle;
            if (pointer.mods.popup) {
                popupGesture_ = true;
                beginEdit(response);
                toggleHardCorner(hit.id);
                endEdit(response, CurveEditorResponse::Commit::Edit);
                return response;
            }
            if (!pointer.primary) {
                gesture_ = Target::None;
                return response;
            }
            Pixel centre;
            for (const auto& [id, pixel] : visibleHandles())
                if (id == hit.id)
                    centre = pixel;
            handleOffsetX_ = static_cast<double>(centre.x) - pointer.x;
            handleOffsetY_ = static_cast<double>(centre.y) - pointer.y;
            lastShaperX_ = centre.x;
            lastShaperY_ = centre.y;
            handleDragged_ = false;
            response.repaint = true;
            return response;
        }

        case Target::Point: {
            if (pointer.mods.popup) {
                response = canvasDown(pointer, px, py);
                return response;
            }
            if (!pointer.primary)
                return response;
            gesture_ = Target::Point;
            if (pointer.mods.shift) {
                if (!selection_.erase(hit.id))
                    selection_.insert(hit.id);
            } else if (!selection_.contains(hit.id) || selection_.size() == 1) {
                // Pressing a point already in a multi-selection keeps it, so the drag moves all.
                selection_ = {hit.id};
            }
            const auto* p = find(hit.id);
            dragStartX_ = p->x;
            dragStartY_ = p->y;
            anchor_ = {px, py};
            response.repaint = true;
            return response;
        }

        case Target::LoopMarker:
            gesture_ = Target::LoopMarker;
            gestureMarker_ = hit.marker;
            return response;

        case Target::Canvas:
        case Target::None:
            return canvasDown(pointer, px, py);
    }
    return response;
}

CurveEditorResponse CurveEditor::pointerDrag(const CurveEditorPointer& pointer) {
    CurveEditorResponse response;
    const int px = roundToPixel(pointer.x);
    const int py = roundToPixel(pointer.y);

    switch (gesture_) {
        case Target::Canvas:
            if (popupGesture_)
                break;
            if (canvasMode_ == CanvasMode::Select && !drawing_) {
                const int dx = px - anchor_.x;
                const int dy = py - anchor_.y;
                if (!lassoActive_ && dx * dx + dy * dy > kLassoThresholdSquared) {
                    lassoActive_ = true;
                    selection_.clear();
                }
                if (lassoActive_) {
                    lasso_ = {std::min(anchor_.x, px), std::min(anchor_.y, py),
                              std::abs(px - anchor_.x), std::abs(py - anchor_.y)};
                    response.repaint = true;
                }
            } else if (drawing_) {
                pencilPath_.push_back({px, py});
                response.repaint = true;
            }
            break;

        case Target::Point: {
            const double ppx = usableWidth() > 0.0 ? usableWidth() : 100.0;
            const double ppy = usableHeight() > 0.0 ? usableHeight() : 100.0;
            const double x = std::max(0.0, dragStartX_ + (px - anchor_.x) / ppx);
            const double y = std::clamp(dragStartY_ - (py - anchor_.y) / ppy, 0.0, 1.0);
            pointPreview(gestureId_, x, y);
            response.preview = true;
            response.repaint = true;
            break;
        }

        case Target::Handle:
            if (popupGesture_)
                break;
            lastShaperX_ = pointer.x + handleOffsetX_;
            lastShaperY_ = pointer.y + handleOffsetY_;
            handleDragged_ = true;
            if (const auto shaper = shaperFromPixel(gestureId_, lastShaperX_, lastShaperY_)) {
                shaperPreviewId_ = gestureId_;
                std::tie(shaperPreviewX_, shaperPreviewY_) = *shaper;
                response.preview = true;
                response.repaint = true;
            }
            break;

        case Target::LoopMarker: {
            auto phase = static_cast<float>(std::clamp(pixelToX(px), 0.0, 1.0));
            if (snapLoop_ && gridX_ > 1) {
                const double step = 1.0 / gridX_;
                phase = static_cast<float>(std::clamp(std::round(phase / step) * step, 0.0, 1.0));
            }
            if (gestureMarker_ == 1)
                loopStart_ = limit(0.0f, loopEnd_ - kLoopMinGap, phase);
            else
                loopEnd_ = limit(loopStart_ + kLoopMinGap, 1.0f, phase);
            response.loopPreview = true;
            response.repaint = true;
            break;
        }

        case Target::None:
            break;
    }
    return response;
}

CurveEditorResponse CurveEditor::pointerUp(const CurveEditorPointer& pointer) {
    CurveEditorResponse response;
    const int px = roundToPixel(pointer.x);
    const int py = roundToPixel(pointer.y);
    const auto gesture = gesture_;
    gesture_ = Target::None;

    switch (gesture) {
        case Target::Canvas:
            if (popupGesture_)
                break;
            if (canvasMode_ == CanvasMode::Select && !drawing_) {
                if (lassoActive_) {
                    lassoActive_ = false;
                    for (const auto& p : points_) {
                        const int cx = xToPixel(p.x);
                        const int cy = yToPixel(p.y);
                        if (cx >= lasso_.x && cx < lasso_.x + lasso_.width && cy >= lasso_.y &&
                            cy < lasso_.y + lasso_.height)
                            selection_.insert(p.id);
                    }
                    lasso_ = {};
                } else {
                    // Adding a point takes a double-click; a click only clears the selection.
                    selection_.clear();
                }
                response.repaint = true;
            } else if (drawing_) {
                drawing_ = false;
                if (canvasMode_ == CanvasMode::Pencil && pencilPath_.size() >= 2) {
                    beginEdit(response);
                    addPencilPoints();
                    endEdit(response, CurveEditorResponse::Commit::Edit);
                }
                pencilPath_.clear();
                response.repaint = true;
            }
            break;

        case Target::Point: {
            if (popupGesture_)
                break;
            const int dx = px - anchor_.x;
            const int dy = py - anchor_.y;
            if (std::abs(dx) > kMoveCommitPixels || std::abs(dy) > kMoveCommitPixels) {
                const double ppx = usableWidth() > 0.0 ? usableWidth() : 100.0;
                const double ppy = usableHeight() > 0.0 ? usableHeight() : 100.0;
                const double x = std::max(0.0, dragStartX_ + dx / ppx);
                const double y = std::clamp(dragStartY_ - dy / ppy, 0.0, 1.0);
                beginEdit(response);
                commitPointMove(gestureId_, x, y);
                endEdit(response, CurveEditorResponse::Commit::Edit);
            } else if (previewLead_) {
                clearPreview();
                response.preview = true;
            }
            response.repaint = true;
            break;
        }

        case Target::Handle:
            if (popupGesture_)
                break;
            shaperPreviewId_.reset();
            if (handleDragged_) {
                if (const auto shaper = shaperFromPixel(gestureId_, lastShaperX_, lastShaperY_)) {
                    beginEdit(response);
                    applyShaper(gestureId_, shaper->first, shaper->second);
                    endEdit(response, CurveEditorResponse::Commit::Edit);
                }
            }
            response.repaint = true;
            break;

        case Target::LoopMarker:
            response.loopCommitted = true;
            response.repaint = true;
            break;

        case Target::None:
            break;
    }

    popupGesture_ = false;
    return response;
}

CurveEditorResponse CurveEditor::doubleClick(const CurveEditorPointer& pointer) {
    CurveEditorResponse response;
    const int px = roundToPixel(pointer.x);
    const int py = roundToPixel(pointer.y);
    const auto hit = hitTest(px, py);

    switch (hit.target) {
        case Target::Point:
            if (points_.size() <= 2)
                break;
            beginEdit(response);
            std::erase_if(points_, [&](const auto& p) { return p.id == hit.id; });
            endEdit(response, CurveEditorResponse::Commit::Edit);
            break;

        case Target::Handle:
            if (!pointer.primary || pointer.mods.popup)
                break;
            // Flattens the segment: the shaper goes to the straight line's midpoint.
            for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
                if (points_[i].id != hit.id)
                    continue;
                const double midX = 0.5 * (points_[i].x + points_[i + 1].x);
                const double midY = 0.5 * (points_[i].y + points_[i + 1].y);
                if (const auto shaper = shaperFromPixel(hit.id, xToPixel(midX), yToPixel(midY))) {
                    beginEdit(response);
                    applyShaper(hit.id, shaper->first, shaper->second);
                    endEdit(response, CurveEditorResponse::Commit::Edit);
                }
                break;
            }
            break;

        case Target::Canvas:
        case Target::LoopMarker:
            if (pointer.mods.popup || canvasMode_ != CanvasMode::Select || drawing_)
                break;
            beginEdit(response);
            addPoint(snapX(pixelToX(px)), std::clamp(snapY(pixelToY(py)), 0.0, 1.0),
                     CurveInterpolation::Linear);
            endEdit(response, CurveEditorResponse::Commit::Edit);
            break;

        case Target::None:
            break;
    }
    return response;
}

CurveEditorResponse CurveEditor::pointerMove(const CurveEditorPointer& pointer) {
    CurveEditorResponse response;
    if (gesture_ != Target::None)
        return response;
    const auto hit = hitTest(roundToPixel(pointer.x), roundToPixel(pointer.y));
    if (hit.target != hover_.target || hit.id != hover_.id) {
        hover_ = hit;
        response.repaint = true;
    }
    return response;
}

CurveEditorResponse CurveEditor::pointerExit() {
    CurveEditorResponse response;
    if (gesture_ == Target::None && hover_.target != Target::None) {
        hover_ = {};
        response.repaint = true;
    }
    return response;
}

CurveEditorResponse CurveEditor::keyPressed(const CurveEditorKey& key) {
    CurveEditorResponse response;
    const auto& mods = key.mods;
    const bool plain = !mods.shift && !mods.command && !mods.alt;

    if (key.code == CurveEditorKey::Code::Delete || key.code == CurveEditorKey::Code::Backspace) {
        response.handled = true;
        if (selection_.empty())
            return response;
        beginEdit(response);
        if (deletePoints(selection_))
            endEdit(response, CurveEditorResponse::Commit::Edit);
        else
            response.before.clear();
        selection_.clear();
        response.repaint = true;
        return response;
    }

    if (key.code != CurveEditorKey::Code::Character)
        return response;

    if (key.character == U'a' && mods.command && !mods.shift && !mods.alt) {
        for (const auto& p : points_)
            selection_.insert(p.id);
        response.handled = true;
        response.repaint = true;
        return response;
    }

    if (key.character == U'c' && plain) {
        showCrosshair_ = !showCrosshair_;
        response.handled = true;
        response.repaint = true;
    }
    return response;
}

CurveEditorCursor CurveEditor::cursor(const CurveEditorModifiers& mods) const {
    const auto target = gesture_ != Target::None ? gesture_ : hover_.target;
    if (target == Target::Handle)
        return CurveEditorCursor::DraggingHand;
    if (target == Target::Point)
        return CurveEditorCursor::Normal;
    if (mods.shift)
        return CurveEditorCursor::PointingHand;
    if (mods.command)
        return CurveEditorCursor::Copying;
    return CurveEditorCursor::Normal;
}

}  // namespace magda::sdk
