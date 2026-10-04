#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "magda/sdk/curve/CurveTypes.hpp"
#include "magda/sdk/display/DisplayList.hpp"

/**
 * @file CurveEditor.hpp
 * @brief The editing surface for a phase curve: points, gestures, hit-testing, snapping and the
 *        frame it draws (docs/curve-editor.md).
 */

namespace magda::sdk {

/// A point as the editor holds it: double precision, with an identity stable across edits.
struct CurveEditorPoint {
    std::uint32_t id = 0;
    double x = 0.0;
    double y = 0.5;
    CurveInterpolation interpolation = CurveInterpolation::Linear;
    double tension = 0.0;
    double inHandleX = 0.0;
    double inHandleY = 0.0;
    double outHandleX = 0.0;
    double outHandleY = 0.0;
};

struct CurveEditorModifiers {
    bool shift = false;
    /// Cmd on macOS, Ctrl elsewhere.
    bool command = false;
    bool alt = false;
    /// The context-menu button: right button, or Ctrl-click on macOS.
    bool popup = false;
};

/// A pointer event in surface pixels, origin top left.
struct CurveEditorPointer {
    float x = 0.0f;
    float y = 0.0f;
    CurveEditorModifiers mods;
    /// The primary button is down (or went down, for a press).
    bool primary = true;
};

struct CurveEditorKey {
    enum class Code { Character, Delete, Backspace, Other };
    Code code = Code::Other;
    /// Lowercase for letters.
    char32_t character = 0;
    CurveEditorModifiers mods;
};

enum class CurveEditorCursor { Normal, PointingHand, Copying, DraggingHand };

/// What an event did; the shell persists, records undo and repaints from it.
struct CurveEditorResponse {
    enum class Commit { None, Edit, StampStep };

    bool repaint = false;
    /// The curve the engine should hear moved inside a gesture; read effectivePoints().
    bool preview = false;
    /// One undo step: the points before it are in @c before, the result in points().
    Commit commit = Commit::None;
    std::vector<CurvePointData> before;
    /// The loop region moved inside a gesture, or the gesture ended.
    bool loopPreview = false;
    bool loopCommitted = false;
    /// Keys only: the editor used the key.
    bool handled = false;
};

/**
 * @brief Interaction and rendering for a curve over one cycle (the phase domain).
 *
 * The curve loops: the first point is pinned to x = 0 and the last to x = 1. The editor keeps
 * at least two points. Every gesture commits at most once, so a shell records one undo step
 * per CurveEditorResponse::Commit.
 */
class CurveEditor {
  public:
    using TextMeasure = std::function<float(std::string_view text, float fontSize)>;

    CurveEditor();

    // Model

    /// Replaces the curve and renumbers the points; cancels any gesture and clears selection.
    void setPoints(std::span<const CurvePointData> points);
    /// Updates values in place when the count is unchanged (identities and gestures survive),
    /// otherwise setPoints().
    void refreshPoints(std::span<const CurvePointData> points);
    /// Copies values onto the points the two lists share, without pinning or re-sorting.
    void syncPoints(std::span<const CurvePointData> points);

    /// The committed curve.
    std::vector<CurvePointData> points() const;
    /// The committed curve with the gesture in progress applied, in model order.
    std::vector<CurvePointData> effectivePoints() const;
    const std::vector<CurveEditorPoint>& editorPoints() const {
        return points_;
    }

    // Surface

    void setSize(int width, int height);
    void setPadding(int padding);
    int padding() const {
        return padding_;
    }

    void setGrid(int divisionsX, int divisionsY);
    int gridDivisionsX() const {
        return gridX_;
    }
    int gridDivisionsY() const {
        return gridY_;
    }
    void setSnap(bool x, bool y, bool loop);

    /// Shown only when @p visible; markers drag only then.
    void setLoopRegion(bool visible, float start, float end);
    float loopStart() const {
        return loopStart_;
    }
    float loopEnd() const {
        return loopEnd_;
    }

    void setDrawContentBorder(bool draw) {
        drawContentBorder_ = draw;
    }
    void setShowCrosshair(bool show) {
        showCrosshair_ = show;
    }
    bool showCrosshair() const {
        return showCrosshair_;
    }
    /// The modulator's live position. Hidden until set.
    void setIndicator(float phase, float value, bool triggerLit);
    void clearIndicator() {
        indicatorVisible_ = false;
    }

    /// Width of @p text at @p fontSize, for the value tooltip.
    void setTextMeasure(TextMeasure measure) {
        measure_ = std::move(measure);
    }

    // Input

    CurveEditorResponse pointerDown(const CurveEditorPointer& pointer);
    CurveEditorResponse pointerDrag(const CurveEditorPointer& pointer);
    CurveEditorResponse pointerUp(const CurveEditorPointer& pointer);
    CurveEditorResponse doubleClick(const CurveEditorPointer& pointer);
    /// A move with no button down: hover.
    CurveEditorResponse pointerMove(const CurveEditorPointer& pointer);
    CurveEditorResponse pointerExit();
    CurveEditorResponse keyPressed(const CurveEditorKey& key);

    CurveEditorCursor cursor(const CurveEditorModifiers& mods) const;

    const std::set<std::uint32_t>& selection() const {
        return selection_;
    }

    // Mapping

    double pixelToX(int px) const;
    double pixelToY(int py) const;
    int xToPixel(double x) const;
    int yToPixel(double y) const;
    double xToPixelF(double x) const;
    double yToPixelF(double y) const;

    /// Replaces @p out with the current frame.
    void render(display::DisplayList& out) const;

  private:
    struct Rect {
        int x = 0, y = 0, width = 0, height = 0;
    };
    enum class Target { None, Canvas, Point, Handle, LoopMarker };
    enum class CanvasMode { Select, Pencil, Stamp };
    struct Hit {
        Target target = Target::None;
        std::uint32_t id = 0;
        int marker = 0;
    };
    struct Pixel {
        int x = 0, y = 0;
    };

    Rect content() const;
    double usableWidth() const;
    double usableHeight() const;
    float pointScale() const;

    Hit hitTest(int px, int py) const;
    int loopMarkerAt(int px, int py) const;
    std::vector<const CurveEditorPoint*> renderOrder() const;
    std::pair<double, double> effectivePosition(const CurveEditorPoint& p) const;
    bool hasStoredShaper(const CurveEditorPoint& p1, const CurveEditorPoint& p2) const;
    std::pair<double, double> shaperPosition(const CurveEditorPoint& p1,
                                             const CurveEditorPoint& p2) const;
    std::pair<double, double> handlePosition(const CurveEditorPoint& p1,
                                             const CurveEditorPoint& p2) const;
    /// Visible segment handles, keyed by the owning (left) point, at their pixel centres.
    std::vector<std::pair<std::uint32_t, Pixel>> visibleHandles() const;
    const CurveEditorPoint* find(std::uint32_t id) const;
    CurveEditorPoint* find(std::uint32_t id);
    std::uint32_t segmentOwnerAt(double x) const;

    double snapX(double x) const;
    double snapY(double y) const;
    void constrain(std::uint32_t id, double& x, double& y) const;

    void pointPreview(std::uint32_t id, double x, double y);
    void commitPointMove(std::uint32_t id, double x, double y);
    /// Shaper control from a cursor position; nullopt when @p id owns no segment.
    std::optional<std::pair<double, double>> shaperFromPixel(std::uint32_t id, double px,
                                                             double py) const;
    void applyShaper(std::uint32_t id, double cx, double cy);
    void toggleHardCorner(std::uint32_t id);
    void addPoint(double x, double y, CurveInterpolation interpolation);
    bool deletePoints(const std::set<std::uint32_t>& ids);
    void stampStep(double gridStart, double gridEnd, double y, std::uint32_t prevId,
                   double prevValue);
    void addPencilPoints();
    void clearPreview();
    void sortPoints();
    void beginEdit(CurveEditorResponse& response) const;
    void endEdit(CurveEditorResponse& response, CurveEditorResponse::Commit kind) const;

    CurveEditorResponse canvasDown(const CurveEditorPointer& pointer, int px, int py);

    void renderGrid(display::DisplayList& out) const;
    void renderCurve(display::DisplayList& out) const;
    void renderIndicator(display::DisplayList& out) const;
    void renderPoints(display::DisplayList& out) const;
    void renderTooltip(display::DisplayList& out) const;

    std::vector<CurveEditorPoint> points_;
    std::uint32_t nextId_ = 1;
    std::set<std::uint32_t> selection_;

    int width_ = 0;
    int height_ = 0;
    int padding_ = 8;
    int gridX_ = 4;
    int gridY_ = 4;
    bool snapX_ = false;
    bool snapY_ = false;
    bool snapLoop_ = true;
    bool loopVisible_ = false;
    float loopStart_ = 0.0f;
    float loopEnd_ = 1.0f;
    bool drawContentBorder_ = false;
    bool showCrosshair_ = false;
    bool indicatorVisible_ = false;
    float indicatorPhase_ = 0.0f;
    float indicatorValue_ = 0.0f;
    bool triggerLit_ = false;
    TextMeasure measure_;

    // Gesture state
    Target gesture_ = Target::None;
    std::uint32_t gestureId_ = 0;
    int gestureMarker_ = 0;
    CanvasMode canvasMode_ = CanvasMode::Select;
    bool popupGesture_ = false;
    Pixel anchor_;
    bool lassoActive_ = false;
    Rect lasso_;
    bool drawing_ = false;
    std::vector<Pixel> pencilPath_;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;
    std::map<std::uint32_t, std::pair<double, double>> multiStart_;
    double handleOffsetX_ = 0.0;
    double handleOffsetY_ = 0.0;
    double lastShaperX_ = 0.0;
    double lastShaperY_ = 0.0;
    bool handleDragged_ = false;

    // Preview state
    std::optional<std::uint32_t> previewLead_;
    std::map<std::uint32_t, std::pair<double, double>> previewPositions_;
    std::optional<std::uint32_t> shaperPreviewId_;
    double shaperPreviewX_ = 0.0;
    double shaperPreviewY_ = 0.0;

    // Hover
    Hit hover_;
};

}  // namespace magda::sdk
