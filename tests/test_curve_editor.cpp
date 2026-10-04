#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/sdk/curve/CurveMath.hpp"
#include "magda/sdk/curveedit/CurveEditor.hpp"

using Catch::Approx;
using magda::sdk::CurveEditor;
using magda::sdk::CurveEditorKey;
using magda::sdk::CurveEditorPointer;
using magda::sdk::CurveEditorResponse;
using magda::sdk::CurveInterpolation;
using magda::sdk::CurvePointData;

namespace {

using Commit = CurveEditorResponse::Commit;

CurvePointData point(float phase, float value, int curveType = 0) {
    CurvePointData p;
    p.phase = phase;
    p.value = value;
    p.curveType = curveType;
    return p;
}

/// 200 x 120 with padding 8: x pixels run 16..184, y pixels 16 (value 1) to 104 (value 0).
CurveEditor editorWith(std::vector<CurvePointData> points) {
    CurveEditor editor;
    editor.setSize(200, 120);
    editor.setPoints(points);
    return editor;
}

CurveEditor triangle() {
    return editorWith({point(0, 0), point(0.5f, 1), point(1, 0)});
}

CurveEditorPointer at(float x, float y) {
    CurveEditorPointer p;
    p.x = x;
    p.y = y;
    return p;
}

CurveEditorPointer withShift(CurveEditorPointer p) {
    p.mods.shift = true;
    return p;
}

CurveEditorPointer withCommand(CurveEditorPointer p) {
    p.mods.command = true;
    return p;
}

CurveEditorPointer popup(CurveEditorPointer p) {
    p.mods.popup = true;
    p.primary = false;
    return p;
}

CurveEditorResponse click(CurveEditor& editor, CurveEditorPointer p) {
    editor.pointerDown(p);
    return editor.pointerUp(p);
}

CurveEditorResponse drag(CurveEditor& editor, CurveEditorPointer from, CurveEditorPointer to) {
    editor.pointerDown(from);
    editor.pointerDrag(to);
    return editor.pointerUp(to);
}

CurveEditorKey key(CurveEditorKey::Code code, char32_t character = 0, bool command = false) {
    CurveEditorKey k;
    k.code = code;
    k.character = character;
    k.mods.command = command;
    return k;
}

}  // namespace

TEST_CASE("The editor pins the curve's ends and maps through the edge inset", "[curveedit]") {
    auto editor = editorWith({point(0.1f, 0.2f), point(0.9f, 0.4f)});
    const auto points = editor.points();
    CHECK(points.front().phase == 0.0f);
    CHECK(points.back().phase == 1.0f);

    CHECK(editor.xToPixel(0.0) == 16);
    CHECK(editor.xToPixel(1.0) == 184);
    CHECK(editor.yToPixel(1.0) == 16);
    CHECK(editor.yToPixel(0.0) == 104);
    CHECK(editor.pixelToX(100) == Approx(0.5));
    CHECK(editor.pixelToY(60) == Approx(0.5));
}

TEST_CASE("A single click clears the selection and a double-click adds a point", "[curveedit]") {
    auto editor = triangle();
    editor.keyPressed(key(CurveEditorKey::Code::Character, U'a', true));
    REQUIRE(editor.selection().size() == 3);

    CHECK(click(editor, at(150, 90)).commit == Commit::None);
    CHECK(editor.selection().empty());

    const auto response = editor.doubleClick(at(150, 90));
    CHECK(response.commit == Commit::Edit);
    CHECK(response.before.size() == 3);
    const auto points = editor.points();
    REQUIRE(points.size() == 4);
    CHECK(points[2].phase == Approx(134.0f / 168.0f));
    CHECK(points[2].value == Approx(1.0f - 74.0f / 88.0f));
}

TEST_CASE("Double-clicking a point deletes it, down to two points", "[curveedit]") {
    auto editor = triangle();
    CHECK(editor.doubleClick(at(100, 16)).commit == Commit::Edit);
    CHECK(editor.points().size() == 2);
    CHECK(editor.doubleClick(at(16, 104)).commit == Commit::None);
    CHECK(editor.points().size() == 2);
}

TEST_CASE("Dragging a point previews, then commits once", "[curveedit]") {
    auto editor = triangle();
    editor.pointerDown(at(100, 16));
    const auto preview = editor.pointerDrag(at(142, 38));
    CHECK(preview.preview);
    CHECK(preview.commit == Commit::None);
    CHECK(editor.points()[1].phase == 0.5f);
    CHECK(editor.effectivePoints()[1].phase == Approx(0.75f));
    CHECK(editor.effectivePoints()[1].value == Approx(0.75f));

    const auto up = editor.pointerUp(at(142, 38));
    CHECK(up.commit == Commit::Edit);
    CHECK(up.before[1].phase == 0.5f);
    CHECK(editor.points()[1].phase == Approx(0.75f));
    CHECK(editor.points()[1].value == Approx(0.75f));
}

TEST_CASE("A drag of two pixels or less commits nothing and drops the preview", "[curveedit]") {
    auto editor = triangle();
    editor.pointerDown(at(100, 16));
    editor.pointerDrag(at(102, 18));
    const auto up = editor.pointerUp(at(102, 18));
    CHECK(up.commit == Commit::None);
    CHECK(up.preview);
    CHECK(editor.effectivePoints()[1].phase == 0.5f);
}

TEST_CASE("Edge points stay pinned and interior points snap to the grid", "[curveedit]") {
    auto editor = triangle();
    editor.setSnap(true, true, true);
    drag(editor, at(16, 104), at(60, 60));
    CHECK(editor.points().front().phase == 0.0f);
    CHECK(editor.points().front().value == 0.5f);

    drag(editor, at(100, 16), at(122, 20));
    CHECK(editor.points()[1].phase == 0.75f);
    CHECK(editor.points()[1].value == 1.0f);
}

TEST_CASE("Shift-click extends the selection and the drag moves it together", "[curveedit]") {
    auto editor = editorWith({point(0, 0), point(0.25f, 0.5f), point(0.5f, 1), point(1, 0)});
    click(editor, at(58, 60));
    editor.pointerDown(withShift(at(100, 16)));
    CHECK(editor.selection().size() == 2);
    editor.pointerDrag(withShift(at(121, 38)));
    const auto up = editor.pointerUp(withShift(at(121, 38)));
    CHECK(up.commit == Commit::Edit);
    const auto points = editor.points();
    CHECK(points[1].phase == Approx(0.375f));
    CHECK(points[1].value == Approx(0.25f));
    CHECK(points[2].phase == Approx(0.625f));
    CHECK(points[2].value == Approx(0.75f));
}

TEST_CASE("A lasso selects the points whose centres it covers", "[curveedit]") {
    auto editor = triangle();
    editor.pointerDown(at(60, 10));
    editor.pointerDrag(at(190, 50));
    editor.pointerUp(at(190, 50));
    CHECK(editor.selection().size() == 1);
}

TEST_CASE("Delete removes the selection but keeps two points", "[curveedit]") {
    auto editor = editorWith({point(0, 0), point(0.25f, 0.5f), point(0.5f, 1), point(1, 0)});
    editor.keyPressed(key(CurveEditorKey::Code::Character, U'a', true));
    const auto response = editor.keyPressed(key(CurveEditorKey::Code::Delete));
    CHECK(response.handled);
    CHECK(response.commit == Commit::Edit);
    const auto points = editor.points();
    REQUIRE(points.size() == 2);
    CHECK(points[0].phase == 0.5f);
    CHECK(editor.selection().empty());
}

TEST_CASE("C toggles the crosshair; other keys pass through", "[curveedit]") {
    auto editor = triangle();
    CHECK(editor.keyPressed(key(CurveEditorKey::Code::Character, U'c')).handled);
    CHECK(editor.showCrosshair());
    CHECK_FALSE(editor.keyPressed(key(CurveEditorKey::Code::Character, U'z', true)).handled);
}

TEST_CASE("Right-click turns a bent segment into a hard corner at its midpoint", "[curveedit]") {
    auto bent = point(0, 0);
    bent.tension = 0.6f;
    auto editor = editorWith({bent, point(0.5f, 1), point(1, 0)});
    const auto response = editor.pointerDown(popup(at(40, 100)));
    CHECK(response.commit == Commit::Edit);
    CHECK(editor.pointerUp(popup(at(40, 100))).commit == Commit::None);
    const auto points = editor.points();
    CHECK(points[0].curveType == static_cast<int>(CurveInterpolation::HardCorner));
    const float apexY = magda::sdk::curvemath::evalSegment(0.0f, 1.0f, 0.5f, 0.6f, false, 0.5f);
    CHECK(points[0].outHandleX == Approx(0.25f));
    CHECK(points[0].outHandleY == Approx(apexY));
    CHECK(points[1].inHandleX == Approx(-0.25f));

    click(editor, popup(at(40, 100)));
    CHECK(editor.points()[0].curveType == static_cast<int>(CurveInterpolation::Linear));
}

TEST_CASE("Dragging a segment handle bends it; a double-click flattens it", "[curveedit]") {
    auto editor = triangle();
    editor.pointerDown(at(58, 60));
    CHECK(editor.pointerDrag(at(58, 82)).preview);
    CHECK(editor.points()[0].outHandleY == 0.0f);
    const auto up = editor.pointerUp(at(58, 82));
    CHECK(up.commit == Commit::Edit);
    // The cursor sets the on-curve midpoint: 0.25, so the control is 2 * 0.25 - 0.5 = 0.
    const auto bent = editor.points();
    CHECK(bent[0].outHandleX == Approx(0.25f));
    CHECK(bent[0].outHandleY == Approx(0.0f));
    CHECK(bent[1].inHandleY == Approx(-1.0f));

    CHECK(editor.doubleClick(at(58, 82)).commit == Commit::Edit);
    CHECK(editor.points()[0].outHandleY == Approx(0.5f));
}

TEST_CASE("Clicking a segment handle without dragging leaves the bend alone", "[curveedit]") {
    auto editor = triangle();
    CHECK(click(editor, at(58, 60)).commit == Commit::None);
    CHECK(editor.points()[0].outHandleX == 0.0f);
}

TEST_CASE("Shift-click stamps a step cell one grid step wide", "[curveedit]") {
    auto editor = editorWith({point(0, 0.5f), point(1, 0.5f)});
    editor.setSnap(true, false, true);
    const auto response = editor.pointerDown(withShift(at(100, 38)));
    CHECK(response.commit == Commit::StampStep);
    editor.pointerUp(at(100, 38));
    const auto points = editor.points();
    REQUIRE(points.size() == 4);
    CHECK(points[0].curveType == static_cast<int>(CurveInterpolation::Step));
    CHECK(points[1].phase == 0.5f);
    CHECK(points[1].value == Approx(0.75f));
    CHECK(points[1].curveType == static_cast<int>(CurveInterpolation::Step));
    CHECK(points[2].phase == 0.75f);
    CHECK(points[2].value == 0.5f);
}

TEST_CASE("A pencil stroke adds its points as one edit", "[curveedit]") {
    auto editor = editorWith({point(0, 0), point(1, 0)});
    editor.pointerDown(withCommand(at(30, 90)));
    editor.pointerDrag(at(50, 60));
    editor.pointerDrag(at(54, 58));
    editor.pointerDrag(at(80, 40));
    const auto up = editor.pointerUp(at(80, 40));
    CHECK(up.commit == Commit::Edit);
    CHECK(editor.points().size() == 2 + 3);
}

TEST_CASE("Loop markers drag in the top strip, snapped and kept apart", "[curveedit]") {
    auto editor = triangle();
    editor.setGrid(8, 4);
    editor.setLoopRegion(true, 0.25f, 0.75f);
    editor.pointerDown(at(58, 12));
    const auto moved = editor.pointerDrag(at(30, 12));
    CHECK(moved.loopPreview);
    CHECK(editor.loopStart() == 0.125f);
    CHECK(editor.pointerUp(at(30, 12)).loopCommitted);

    editor.pointerDown(at(37, 12));
    editor.pointerDrag(at(184, 12));
    editor.pointerUp(at(184, 12));
    CHECK(editor.loopStart() == Approx(0.73f));
}

TEST_CASE("Refreshing with the same count keeps identities; a new curve does not", "[curveedit]") {
    auto editor = triangle();
    const auto ids = editor.editorPoints();
    editor.keyPressed(key(CurveEditorKey::Code::Character, U'a', true));

    editor.refreshPoints(std::vector{point(0, 0.1f), point(0.4f, 0.9f), point(1, 0.2f)});
    CHECK(editor.editorPoints()[1].id == ids[1].id);
    CHECK(editor.selection().size() == 3);

    editor.refreshPoints(std::vector{point(0, 0), point(1, 1)});
    CHECK(editor.selection().empty());
    CHECK(editor.points().size() == 2);
}
