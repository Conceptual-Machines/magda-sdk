# Step sequencer cores

The mono and poly step sequencers and their clock, in `core/magda/sdk/sequencer/`. JUCE-free and
allocation-free: the host hands each block's timing and the pattern in, and reads notes out of a
`NoteSink`.

- `StepPattern.hpp`: `MonoPattern` and `PolyPattern`, plain values the host owns.
- `StepClock.hpp`: tempo-synced stepping with swing, direction, the time bend and quantize.
- `MonoStepSequencer.hpp`, `PolyStepSequencer.hpp`: pattern and block in, notes out.
- `NoteSink.hpp`: the output interface, and `NoteEventList` for callers with no buffer.
- `PublishedPattern.hpp`: hands a pattern from the message thread to one audio thread.
- `TimeBend.hpp`: the read-head curve the steps play through (docs/time-bend.md).

## Block timing

`StepClock::BlockTiming` carries the block's start and end beat, whether the transport is
playing, the block length in samples, and the beats per bar the bend's span counts in. Event
times are seconds from the block start.

The clock keeps no position (#22). Tick `k` sits at pattern beat `k * rate` from beat 0, odd
ticks swung later in pattern time, and plays where the bend's read head crosses it; quantize
then pulls it toward `quantizeSub` divisions of the bend's cycle. Its step comes from `k`:
Forward `k mod n`, Reverse `-k mod n`, PingPong over a period of `2n - 2`, Random a hash of the
seed and `k`. Starting mid-pattern, a loop wrap or a rate switch lands on the step the grid has
there, and an offline render plays what the device plays. A block keeps its earliest
`maxEvents` steps.

## Random

The Random direction hashes the tick with a seed; `setDirectionSeed` sets it. The poly
probability roll has its own seed in `setRandomSeed`.

## Floating point

`tests/test_step_sequencer_identity.cpp` pins the notes of a fixed corpus
(`tests/support/StepCorpus.hpp`) to what magda-core played before the cores moved. The note and
velocity hashes hold everywhere; the sample offsets and lengths come from double beat arithmetic
and are checked only with `MAGDA_SDK_EXACT_PINS`.
