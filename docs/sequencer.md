# Step sequencer cores

The mono and poly step sequencers and their clock, in `core/magda/sdk/sequencer/`. JUCE-free and
allocation-free: the host hands each block's timing and the pattern in, and reads notes out of a
`NoteSink`.

- `StepPattern.hpp`: `MonoPattern` and `PolyPattern`, plain values the host owns.
- `StepClock.hpp`: tempo-synced stepping with swing, direction, ramp warp and quantize.
- `MonoStepSequencer.hpp`, `PolyStepSequencer.hpp`: pattern and block in, notes out.
- `NoteSink.hpp`: the output interface, and `NoteEventList` for callers with no buffer.
- `PublishedPattern.hpp`: hands a pattern from the message thread to one audio thread.
- `RampCurve.hpp`: the timing curve the clock and the arpeggiator share.

## Block timing

`StepClock::BlockTiming` carries the block's start and end beat, whether the transport is
playing, and the block length in samples. Event times are seconds from the block start.

## Random

The Random direction draws from `Lcg48Random`, entropy-seeded. `setDirectionSeed` fixes it for a
test; the poly probability roll has its own seed in `setRandomSeed`.

## Floating point

`tests/test_step_sequencer_identity.cpp` pins the notes of a fixed corpus
(`tests/support/StepCorpus.hpp`) to what magda-core played before the cores moved. The note and
velocity hashes hold everywhere; the sample offsets and lengths come from double beat arithmetic
and are checked only with `MAGDA_SDK_EXACT_PINS`.
