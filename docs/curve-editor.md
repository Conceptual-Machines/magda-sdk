# Curve editor

`core/magda/sdk/curveedit/`. The editing surface for a phase curve (an LFO cycle, a sidechain
gain shape): points, gestures, hit-testing, snapping and undo grouping, drawn as a display list
(docs/display-list.md). A shell forwards pointer and key events in pixels and acts on what each
returns. Beat-domain curves (automation) are not covered yet.

## Model

Points are `CurvePointData` at the boundary and doubles inside, each with an id that is stable
across edits. The first point sits at x = 0 and the last at x = 1, and the editor never goes
below two points.

| Call | Purpose |
|---|---|
| `setPoints(points)` | A new curve: renumbers, sorts, pins the ends, cancels any gesture, clears selection. |
| `refreshPoints(points)` | The same curve with new values (after an undo, or a write from elsewhere): in place when the count matches, so ids, selection and a gesture survive; otherwise `setPoints`. |
| `syncPoints(points)` | Values onto the shared prefix, nothing else: a second editor following a drag. |
| `points()` | The committed curve. |
| `effectivePoints()` | The committed curve with the gesture in progress applied, for the engine to hear mid-drag. |

## Events

`pointerDown`, `pointerDrag`, `pointerUp`, `doubleClick` (after the second up, as JUCE sends
it), `pointerMove` (hover), `pointerExit` and `keyPressed`. Each returns a `CurveEditorResponse`:

- `preview`: the effective curve moved; push `effectivePoints()` to whatever plays it.
- `commit`: one undo step, with the points before it in `before` and the result in `points()`.
  `StampStep` names a step stamp, `Edit` everything else. Every gesture commits at most once.
- `loopPreview` / `loopCommitted`: a loop marker moved, or its drag ended. Not an undo step.
- `repaint`; for keys, `handled`.

Undo and redo are the shell's: it keeps `before` and restores with `refreshPoints`.

## Gestures

Targets are tested topmost first: segment handles, then points (later ones on top), then the loop
markers, then the canvas.

| Gesture | Effect |
|---|---|
| Double-click empty space | Adds a Linear point, snapped. A single click clears the selection. |
| Double-click a point | Deletes it. |
| Drag a point | Moves it, and the rest of the selection with it. Ends stay at 0 and 1. Two pixels or less is a click. |
| Shift-click a point | Toggles it in the selection. |
| Drag empty space | Lasso, after 4 pixels; selects the points whose centres it covers. |
| Shift-click empty space | Stamps a step cell one X grid step wide: the point before it turns Step, a Step point at the click, a Linear point at the cell's end returning to the previous value. |
| Cmd-drag | Pencil: a point every 10 pixels and at the end, one edit. |
| Drag a segment handle | Linear: bends vertically, the cursor being the on-curve midpoint. Hard corner: moves the apex, kept inside the segment and the range. |
| Double-click a segment handle | Flattens the segment. |
| Right-click a segment or its handle | Toggles a hard corner, its apex where the curve passed. |
| Drag a loop marker (top 12 px) | Moves it, snapped to the X grid when loop snap is on, at least 0.02 from the other. |
| Delete, Backspace | Deletes the selection, in model order, down to two points. |
| Cmd-A | Selects every point. |
| C | Toggles the crosshair. |

Snapping rounds to `1 / divisions` on each axis when it is on and there is more than one
division.

## Surface

`setSize`, `setPadding` (8 by default; the field inside it is inset a further 8 pixels so edge
points stay grabbable), `setGrid`, `setSnap`, `setLoopRegion`, `setDrawContentBorder`,
`setIndicator` (the modulator's live phase and value, and whether its trigger lamp is lit) and
`setTextMeasure` (for the value tooltip; six units per character without it). `cursor(mods)`
names the pointer the shell should show.

The frame: background, grid, loop shading and markers, the curve and its fill, the pencil stroke,
the lasso, the content border, the indicator, the points, the segment handles and the value
tooltip, in that order. The curve is sampled through `curvemath::evalSegment`, so it is drawn as
the engine plays it.

## Goldens

`tests/golden/curve-editor/scenarios.json` lists cases (a surface, points and a sequence of
events); each `<name>.json` beside it is the final frame at three decimals, with tooltips sized
at six units per character. `tests/test_curve_editor_golden.cpp` checks them natively,
`hosts/canvas/check.mjs` through the wasm build. `MAGDA_SDK_UPDATE_GOLDENS=1 magda_sdk_tests
"[curveedit][golden]"` rewrites them.
