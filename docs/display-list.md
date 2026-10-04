# Display list

`core/magda/sdk/display/`. What a UI core emits instead of drawing: a list of commands over a
surface of `width` by `height` units, origin top left, y down. A shell interprets it with its
own graphics API and maps the colour roles onto its theme. Interpreters:
`hosts/juce/DisplayListGraphics` (`juce::Graphics`) and `hosts/canvas/js/display-list.js`
(Canvas 2D).

## Commands

| Command | Fields | Notes |
|---|---|---|
| `fillRect` | `rect`, `radius`, `paint` | `radius` 0 is a sharp rectangle. |
| `strokeRect` | `rect`, `radius`, `lineWidth`, `paint` | Stroke centred on the edge. |
| `fillPath` | `path`, `paint` | Non-zero winding. |
| `strokePath` | `path`, `lineWidth`, `join`, `cap`, `paint` | `join` is `miter` (default), `round` or `bevel`; `cap` is `butt` (default), `round` or `square`. |
| `fillEllipse` | `rect`, `paint` | The ellipse inscribed in `rect`. |
| `strokeEllipse` | `rect`, `lineWidth`, `paint` | Stroke centred on that ellipse. |
| `text` | `text`, `rect`, `fontSize`, `justification`, `colour` | One line, vertically centred, clipped to `rect`. UTF-8. |
| `clipRect` | `rect` | Intersects the clip, snapped outward to whole units, until the matching `restore`. |
| `save`, `restore` | | Clip state. |

A path is a list of `M x y`, `L x y`, `Q cx cy x y`, `C c1x c1y c2x c2y x y` and `Z`.

## Paint and colour

A paint is a solid colour or a linear gradient: `from`, `to` and stops sorted by position from
0 to 1. A colour is either a role or a literal ARGB. An optional `brighter` lightens it as
`juce::Colour::brighter` does (each channel becomes `255 - (255 - c) / (1 + brighter)`,
truncated), then an optional `alpha` replaces its alpha (as `juce::Colour::withAlpha` does, not
multiplied).

| Role | Reference ARGB | Meaning |
|---|---|---|
| `background` | `FF1E1E1E` | Behind everything a core draws. |
| `surface` | `FF2A2A2A` | A raised area: a meter track, a panel. |
| `border` | `FF444444` | Outlines and reference ticks. |
| `text` | `FFE0E0E0` | Primary text. |
| `textDim` | `FF909090` | Secondary text, scales. |
| `accent` | `FF5B9BD5` | Selection and focus. |
| `meterLow` | `FF55AA55` | Level below -12 dB. |
| `meterMid` | `FFAAAA55` | Level from -12 to 0 dB. |
| `meterHigh` | `FFAA5555` | Level above 0 dB. |
| `meterClip` | `FFFF3B3B` | A clip latch. |
| `textBright` | `FFFFFFFF` | High-contrast marks: grid lines at low alpha, selection rings. |
| `curve` | `FFE8A33D` | The edited curve, and marks drawn in its colour. |
| `curvePoint` | `FFF0F0F0` | A curve's points. |
| `handle` | `FF1E1E1E` | Fill of a draggable handle. |
| `handleStroke` | `FF8A8A8A` | Outline of a handle at rest. |
| `tooltip` | `E0101010` | Value tooltip background. |
| `tooltipText` | `FFF0F0F0` | Value tooltip text. |
| `guide` | `FF3A3A3A` | Frames and guides. |
| `shade` | `FF000000` | Shading over an inactive region, at low alpha. |

The reference palette is `defaultColour()` in C++ and `defaultPalette` in JS; a shell maps
whichever roles its theme has and leaves the rest to the reference. The roles are deliberately
few: a shell's own palette is richer, and the mapping is where the two meet.

## Wire form

`toJson(list, {decimals})`:

```json
{"format": "magda.display-list", "version": 1, "width": 12.0, "height": 120.0, "commands": [
  {"op": "fillRect", "rect": [0, 0, 12, 120], "paint": {"colour": {"role": "background"}}},
  {"op": "fillRect", "rect": [0, 80, 5.5, 40], "radius": 1,
   "paint": {"linear": {"from": [0, 120], "to": [0, 0],
                        "stops": [[0, {"role": "meterLow"}], [1, {"role": "meterHigh"}]]}}},
  {"op": "fillRect", "rect": [0, 29, 12, 1], "paint": {"colour": {"argb": "#FF444444", "alpha": 0.5}}}
]}
```

`decimals` rounds every number, for goldens; the default writes each float's shortest exact
text. Geometry compared across native and wasm builds needs a tolerance, since `std::pow` and
friends differ by an ulp between the two libms.
