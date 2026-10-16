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

## LogMelFrontEnd

`LogMel.hpp`: the log-mel spectrogram audio models take. A Hann-windowed STFT with frames
centred as librosa's `center=True` does (the signal is padded by half a frame at each end, so
`numSamples / hopSize + 1` frames), the bins through a bank of triangular mel filters, then a log.
Output is frame-major, `numFrames x numMels`.

`LogMelConfig` names everything a model's preprocessing fixes:

| Field | Choices |
|---|---|
| `scale` | `Htk` (`2595 log10(1 + f / 700)`) or `Slaney` (linear below 1 kHz, logarithmic above). |
| `spectrum` | `Power` (`re^2 + im^2`) or `Magnitude`. |
| `binScale` | Multiplies each bin, e.g. `1 / sqrt(fftSize)`. |
| `normaliseWindow` | Scale the Hann window to a mean of one. |
| `padding` | `Zero` or `Reflect`. Reflection needs more than `fftSize` samples. |
| `compression` | `Log`: `log(x + logOffset)`; `Log1p`: `log1p(log1pScale * max(x, logOffset))`. |

The filters are unnormalised triangles between `numMels + 2` edges evenly spaced in mels from
`fMin` to `fMax`; `melFilterbank(config)` returns them as `numMels x (fftSize / 2 + 1)`.
`prepare` allocates; `compute` allocates only the padded copy of the input.

## Spectrogram

`Spectrogram.hpp`: a spectrogram to look at, not to feed a model. Each column is a short Hann
window (5.3 ms by default) zero-padded to a 2048-point FFT, so an attack is a sharp vertical edge
and the low rows interpolate smoothly. `numRows` bands (128) are spaced logarithmically from
`lowHz` to `highHz` (30 Hz to 20 kHz), and each row takes the strongest bin in its band, or the
nearest bin where a band is narrower than one. Levels are dB scaled so a full-scale sine reads
about 0 dB in its row.

`column(samples, numSamples, centre, rowsDb)` fills one column for the window centred on a sample
position, reading the signal as zero outside the buffer, so a host can compute any column from a
block that covers its window. The host chooses the hop, the colour map and the dB range it shows.
`prepare` allocates; `column` does not.
