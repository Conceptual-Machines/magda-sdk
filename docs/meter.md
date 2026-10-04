# Meter

`core/magda/sdk/meter/`. A level meter as pure state, and a painter that turns it into a
display list (docs/display-list.md).

## MeterModel

Up to two channels, as `LevelTap` publishes. Nothing reads a clock: the host passes the time
since the last frame, so a run is reproducible.

| Call | Purpose |
|---|---|
| `setTargets(gains)` / `setTargets(LevelTap::Levels)` | A new reading. Clamped to `maxGain`; raises a peak at once; a reading over `clipGain` latches the clip flag. A missing channel reads as the first. |
| `advance(elapsedMs)` | Ballistics and peak hold. True when anything a painter draws moved. |
| `isIdle()` | Display silent and peaks at the floor: a host may stop its timer. |
| `resetPeaks()`, `clearClips()` | Drop peak holds and clip latches, or only the latches. |

Ballistics (`MeterBallistics`, MAGDA's values by default): per 60 Hz frame the display moves
`0.9` of the way up to a louder target and `0.05` of the way down, rescaled to the elapsed time
as `1 - (1 - c)^(elapsed / 16.67 ms)`, so one long advance equals several short ones. A peak
holds `1500` ms and then falls `48` dB per second to `minDb`. A displayed gain below `0.001`
snaps to silence. `MeterBallistics::follow` is the level half on its own, for a bar with no
peak.

`MeterScale` maps dB to a position as `((db - minDb) / (maxDb - minDb))^3`, from -60 to +6 dB.

## Painter

`paintMeter(model, layout, out)` draws a bar per channel (a `surface` track, a gradient fill of
`meterLow`, `meterMid` and `meterHigh` changing at -12 and 0 dB, a peak mark in the colour of its
level), over a `background` fill, with a 0 dB tick in `border` at half alpha. `MeterLayout`
picks vertical or horizontal, and for a vertical meter can pin 0 dB to a given y
(`zeroDbY`), remapping the scale on either side. The clip latch is state only; the painter does
not draw it.

## Goldens

`tests/golden/meter/scenarios.json` lists cases (a layout and steps of `levels` and
`advanceMs`/`repeat`); each `<name>.json` beside it is the final frame at three decimals.
`tests/test_meter_golden.cpp` checks them natively, `hosts/canvas/check.mjs` through the wasm
build. `MAGDA_SDK_UPDATE_GOLDENS=1 magda_sdk_tests "[meter][golden]"` rewrites them.
