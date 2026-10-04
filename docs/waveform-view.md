# Waveform view

`core/magda/sdk/waveview/`. The state and gestures of a zoomable waveform pane, drawn as a display
list (docs/display-list.md). Positions are source seconds: a sample's own time, not the timeline.

## Sources

A `WaveformSource` answers the loudest absolute sample over a range. `BufferWaveformSource` reads
one channel of samples exactly; `PeakDataWaveformSource` reads stored peaks (docs/peaks.md), whose
64-sample buckets show as steps when zoomed far in. The caller keeps the source alive.

## State

| Call | Purpose |
|---|---|
| `setSize(w, h)` | The pane. Zoom never goes below fitting the whole source. |
| `setSource(source, seconds)` | Fits the source to the width and scrolls home. |
| `setGain(g)` | Scales the drawn peaks. |
| `setMarkers(m)` / `markers()` | Start, end, and the loop region with its on flag. |
| `setPlayhead(s)` | Drawn while above zero. |
| `secondsToPixel`, `pixelToSeconds` | Pane mapping. A time draws no further right than the last column, so an end marker stays visible; a pixel reads back inside the source. |

Zoom is pixels per second, from the fit to `kMaxPixelsPerSecond` (5000).

## Gestures

| Gesture | Effect |
|---|---|
| Alt-drag or middle-drag | Scrolls. |
| Cmd-drag | Zooms about the press: up zooms in, on a log scale whose step grows with the zoom and eases off past 80 pixels. |
| `zoomBy(factor, x)` | Wheel zoom about `x`. |
| Drag a marker (5 px) | Moves it; the end marker wins over the start, the start over the loop markers. |
| Drag the loop bar (top 8 px) | Moves the loop region whole, kept inside the source. |
| Shift-click | Places the loop start. |

A response says whether to repaint and whether a marker moved; `cursor(pointer)` names the
pointer.

## Frame

Clipped to the pane: the mirrored peak per pixel column (filled at 0.3 alpha, outlined at 0.5 px),
the loop region and its bar, the start and end markers, the loop markers and the playhead, as
`waveform`, `loopRegion`, `markerStart`, `markerEnd` and `playhead`.

## Goldens

`tests/golden/waveform-view/scenarios.json` lists cases over a synthesised burst (optionally read
through PeakData) with markers and events; `tests/test_waveform_view_golden.cpp` checks them
natively and `hosts/canvas/check.mjs` through the wasm build.
