# Waveform peaks

Header: `magda/sdk/peaks/PeakData.hpp`. JUCE-free and allocation-light, so a host can compute peaks
from decoded audio on any platform, including a browser build.

One bucket summarises `kSamplesPerPeak` (64) source samples as an int16 min and an int16 max,
per channel. The last bucket covers fewer samples when the length is not a multiple of 64. A
sample is clamped to [-1, 1], multiplied by 32767 and truncated toward zero; a bucket with no
orderable sample (all NaN) is silence.

| Call | Purpose |
|---|---|
| `PeakData(numChannels, numSourceSamples)` | Silent buckets for a source of known length. |
| `addBlock(ConstBufferView, firstSample)` | Fills the buckets a block covers. `firstSample` must be a multiple of 64; blocks may be any length and arrive in any order. |
| `getMinMaxForRange(channel, start, end)` | Min and max over a half-open source range, clamped to the source. |
| `fromPacked(numSourceSamples, peaks)` | Rebuilds from stored min, max pairs, one vector per channel. |
| `channelPeaks(channel)` | The pairs, for a host that persists them. |

Persisting is the host's job. magda-core writes the pairs, little-endian and channel after
channel, behind its own header; `tests/test_peak_data.cpp` pins those bytes by hash.
