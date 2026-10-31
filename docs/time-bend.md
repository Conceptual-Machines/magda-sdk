# Time bend

`core/magda/sdk/sequencer/TimeBend.hpp` (#22). A drawn curve maps real time to a read head in
the pattern, and an event plays wherever the head crosses it. Ported from Rhythmica's sequencer
bend; it replaces the depth/skew ramp in `RampCurve.hpp`, which can only stretch step lengths.

## The model

- **Curve:** up to 32 `CurvePointData` points, x (phase) is real time and y (value) the read
  head, both 0 to 1 of the cycle. The ends are pinned to x = 0 and 1. A run is Linear, bent by
  its stored shaper, or a HardCorner; Step reads as Linear, since a jump would roll.
- **Reading it:** rising plays as written, steeper rolls, flat holds (silence), falling reads
  backwards and plays events again.
- **AMOUNT** blends the diagonal into the curve: `read(t) = t + amount * (curve(t) - t)`.
- **Span:** 1, 2 or 4 bars, in the caller's meter (`cycleBeats(beatsPerBar)`). **CYCLES** repeats
  the curve 1 to 8 times across it.

The curve is read through `modcurve::pointsPrecise`: the editor's `modcurve::points` in double,
since a float read moves a hit by up to 2.5e-4 of the cycle.

## Scheduling

`forEachBentHit` walks a fixed grid of 512 cells per cycle, anchored at beat 0. In each cell the
head is a straight line from `ra` to `rb`; an event at pattern beat `p` between them plays at its
real time on that line. Rising cells take `[ra, rb)`, falling ones `(rb, ra]`, so an event on a
cell boundary plays once per crossing. Nothing is kept between calls, so hits do not depend on
where a block starts.

Pattern beats are absolute: cycle `n` reads pattern beats `[n, n + 1)` cycles. The caller maps a
pattern beat to its own event (a step index modulo the pattern, a clip note) in `sources`, and
decides what a note-off does; Rhythmica keeps a step's gate in real time.

`readHeadBeat` gives the head at a real beat, for a playhead or for quantizing recorded input.

## Knots and shapes

`makeTimeBend(knots)` builds a curve from Rhythmica-style `{x, y, bend}` knots. A bend becomes
the segment's stored shaper, with control `0.5 * (y1 + y2) + 0.5 * bend`, and reads as
Rhythmica's segment to within 1e-6 (the handle is stored as a float). `TimeBendShape` lists the
starting shapes: Straight, Rush, Drag, Rush then drag, Hold then catch up, Backwards, There and
back.

## Grooves

`grooveTimeBend(lateness, notesPerBeat, beatsPerBar, amount)` spells a groove as a bend: a
shift per grid step (-1 to 1 of a step), step `k` playing at `(k + lateness / 2) / notesPerBeat`
beats with the points between steps moving with them, which is the groove map magda-core played
before grooves were bends. Step `k` of an `n`-step cycle becomes the point
`((k + lateness / 2) / n, k / n)`; the cycle repeats the groove until a span and a cycle count
divide into it. A late downbeat is a hold at the cycle's start. An early one cannot be spelled,
since the head would have to cross the cycle's top, and is read as on the grid.

## Grooves from a recording

`extractGroove(audio, sampleRate, settings)` (`sequencer/GrooveExtraction.hpp`) reads a groove off a
recorded loop, for `grooveTimeBend`. The hits are the loop's transients (`detectTransients`, its
spacing tightened to half a grid step). The grid is the loop's tempo (given, or its length over
`bars` bars) at `notesPerBeat` steps a beat, from the first hit when that hit is within half a step
of the loop's start. Each hit belongs to its nearest step; the lateness of a step of the figure
(one bar by default) is its hits' mean shift over every repeat, in grooveTimeBend's units, and its
accent the hits' mean level against the loudest step's. Steps with no hit stay on the grid. The
overload on `GrooveHit`s takes onsets found any other way.

## State

`writeTimeBend` / `readTimeBend` use a `TimeBend` node with `on`, `amount`, `bars` and `cycles`,
and a `Point` child per point (`x`, `y`, and `type`, `tension`, `outX`/`outY`, `inX`/`inY` when
set). Reading sanitises: fields are clamped, the ends pinned, and fewer than two points keep the
diagonal.
