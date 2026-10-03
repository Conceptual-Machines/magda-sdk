# Measurement

`core/magda/sdk/analysis/`. JUCE-free; the measurer is header-only, the detector is one
translation unit.

## TrackMeasurer

`TrackMeasurer.hpp`: BS.1770-4 K-weighted loudness (momentary 400 ms, short-term 3 s, gated
integrated), sample peak, optional 4x true peak, correlation, width, and PLR/PSR. It uses the
SDK biquad and polyphase upsampler.

| Call | Thread | Purpose |
|---|---|---|
| `prepare(rate, maxBlockSize, truePeak)` | any | Sets the rate and clears every window. `maxBlockSize` is unused. |
| `process(ConstBufferView)` | audio | Measures a block of any length. One channel is read as L == R. |
| `read()` | message | Lock-free `LevelsSnapshot`. |
| `setSpectrumCaptureEnabled(bool)` | any | Writes a mono downmix into `getSpectrumRing()` (a 4096-sample `SampleRing`). |

The integrated histogram is read with benign races, so a torn read is a slightly stale value.

## TransientDetector

`TransientDetector.hpp`: onset positions in seconds from one channel. The threshold is
`-10 - sensitivity * 30` dB below the source's own peak, found in a first pass.

`detectTransients(ConstBufferView, rate, settings)` takes audio in memory. The block-reader
overload reads the source in order, twice, in blocks of 32768 frames, so a host can analyse a file
without holding it. A trigger's rewind is clamped to the start of its block; the block size is part
of the result.

`BlockPeak.hpp`: `peakMagnitude`, the highest absolute sample, ignoring NaN.
