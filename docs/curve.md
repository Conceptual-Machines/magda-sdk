# Curve, version 1

Normative. A curve is one JSON document holding the points of an LFO, a sidechain gain shape or
an automation lane, in a form any host can read and evaluate. The format is frozen at 1.0.

Header: `magda/sdk/curve/Curve.hpp` (`readCurve`, `writeCurve`, `evaluateCurve`). Evaluators:
`ModCurve.hpp` (phase), `AutomationCurve.hpp` (beats), `CurveMath.hpp` (the shared bend).

## Document

```json
{
  "format": "magda.curve",
  "version": 1,
  "domain": "beats",
  "range": { "min": 0.0, "max": 1.0 },
  "points": [
    { "x": 0.0, "y": 0.2, "interpolation": "linear", "tension": 0.5 },
    { "x": 4.0, "y": 0.9, "interpolation": "bezier",
      "outHandle": { "x": 1.0, "y": 0.3, "linked": false }, "id": 12 },
    { "x": 8.0, "y": 0.4, "interpolation": "step" }
  ]
}
```

| Key | Meaning |
|---|---|
| `format`, `version` | `magda.curve` and 1. |
| `domain` | `phase`: x is a position in a cycle, 0 to 1, and the curve wraps. `beats`: x is a timeline position in beats, the authoritative time domain; no seconds. |
| `range` | The value range, `min` 0 and `max` 1 in version 1. Values are normalised. |
| `points` | Array of points, in x order (equal x allowed). May be empty. |

## Points

| Member | Meaning |
|---|---|
| `x` | A finite number. In `phase`, within [0, 1]. |
| `y` | A finite number, nominally within `range`. Evaluators do not clamp it. |
| `interpolation` | How the run leaving this point is shaped: `linear`, `bezier`, `step`, `hardCorner`. |
| `tension` | Bend of the run leaving this point; omitted when 0. Nominally -3 to +3 in `phase` and -1 to +1 in `beats`; any finite number is read. |
| `inHandle`, `outHandle` | Handle offsets from the point, `{ "x", "y" }`; omitted when both are 0. |
| `id` | `beats` only. The host's identity for the point: an integer other than -1; omitted when -1. |

A handle in `beats` may carry `"linked": false` (the opposite handle does not mirror it); `linked`
is omitted when true and does not exist in `phase`. A point in `phase` has no `id`.

## Evaluation

The point that opens a run shapes it, so the last point's `interpolation` shapes the run that
wraps to the first in `phase`, and nothing in `beats`, where the curve holds before the first and
after the last point. An empty curve answers 0.5.

- `linear`: a straight run, bent by `tension`, or by the handles when either is non-zero, through
  `curvemath::evalSegment`: a bounded power warp through the midpoint the handle encodes.
- `bezier`: in `beats`, the parametric cubic through the handles, solved for the queried beat. In
  `phase` it reads as `linear`.
- `step`: the point's `y` held until the next point.
- `hardCorner`: two straight runs meeting at an apex, the one `outHandle` encodes, or the
  midpoint when it is zero.

`phase` values are single precision and evaluate in single precision; `beats` values are double.
`modcurve::waveform`, `preset`, `shapeAt` and `endValue` evaluate a built-in LFO waveform or the
preset behind a Custom waveform that has no points; those enums are not part of this document.

## Reader and writer

- `readCurve` refuses anything `writeCurve` would not write: an unknown member, a duplicate key,
  points out of x order, a non-finite number, a `phase` number outside single precision, an
  unknown `interpolation`, and the spellings the writer omits (a zero `tension`, a zero handle,
  `linked: true`, `id: -1`). Invalid JSON is `NotJson`; a document without
  `format: "magda.curve"` is `NotACurve`.
- A `version` below 1 is `UnsupportedVersion`. A `version` above 1 is `FutureVersion`, and
  nothing else is read. A host that finds one keeps the file as it is (`isFutureCurve`).
- `writeCurve` writes only version 1, members in the order above, one point per line, and
  numbers as the shortest text that reads back to the same bits (single precision for `phase`).
  Negative zero is kept. A whole number reads as a number: `1` and `1.0` are the same.
