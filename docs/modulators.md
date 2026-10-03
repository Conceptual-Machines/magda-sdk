# Modulator cores

The LFO, envelope, random walk and follower, in `core/magda/sdk/mod/`. JUCE-free, allocation-free
and lock-free: each is a settings struct, a state struct and an `advanceX` that runs once per
block on the audio thread. The host owns publishing, gating and routing; the cores only advance.

- `ModTypes.hpp`: `ModKind`, `ModSync`, `ModRateType`, `LFOTriggerMode`, `LfoRate`, `ModTiming`
  and `ModBlock`.
- `ModLfo.hpp`: `LfoSettings`/`LfoState`, `advanceLfo`, `restartLfo`, and the bar arithmetic
  (`barBeatsOf`, `barFractionOf`, `cycleBeats`, `rateTypeFromLaneValue`).
- `ModAdsr.hpp`, `ModRandom.hpp`, `ModFollower.hpp`: the same shape for the other three.

## Settings and state

Settings are flat and copyable and ride in the host's published table. State is where the
modulator has got to and belongs to the host's runtime, so a knob move that republishes settings
leaves the phase alone.

## Block timing

A modulator advances once per block, from the block's first sample, so every reader in that block
sees one value. `ModBlock` carries what a core reads of the block: `numSamples`, `playing`,
`secondsStart`, `barPosition` and `barsElapsed`. The host resolves the last two from its tempo
map, so a block that spans a tempo or signature change is worth the sum of its spans, each in
its own bar length. A host with no map uses one signature: `barPosition = beats / barBeats` and
`barsElapsed = beatLength / barBeats`, or wall-clock time at `ModTiming` for a stopped block.

A bar is `numerator * 4 / denominator` quarter-note beats, so a 6/8 bar is 3 beats.

## Rate lane indexing

A Rate lane's stored value counts from the first musical division, not from the enum's zero,
because zero is Hertz and Hertz is not a division. `rateTypeFromLaneValue` and
`laneValueFromRateType` apply the shift.

## skipNativeResync and forceZero

`LfoSettings::skipNativeResync` marks a cross-track sidechain LFO: it ignores triggers from the
track it modulates and is driven only by its source.

`LfoState::forceZero` latches one block of output zero after a retrigger. A trigger arrives
mid-block, after that block's parameters are resolved, so the earliest a device sees it is the
next block; the zero makes that block read as a transition. The phase is reset in the same block.

## Floating point

The cores are pinned by hash over a fixed corpus (`tests/test_mod_sample_identity.cpp`) to what
magda-core produced before they moved. Exact hashes hold on Apple arm64, where they were
captured; elsewhere the behavioural tests in `tests/test_mod_*.cpp` are the check.
