# DSP primitives

Header-only, allocation-free to process, in `core/magda/sdk/dsp/`.

- `Biquad.hpp`: `Biquad<T>` (transposed direct form II), `BiquadCoeffs<T>`, and designs in
  `magda::sdk::biquad` (low/high-pass, BS.1770 high shelf and high-pass, RBJ band-pass, notch,
  peaking, shelves). Designs compute in double; `cast<float>()` rounds each coefficient once.
- `Oversampler.hpp`: 2x/4x polyphase upsampler, FIR downsampler and the pair as `Oversampler`.
  One Hann-windowed sinc kernel, 48 taps by default, cutoff at the base-rate Nyquist. The pair's
  latency is `tapsPerPhase - 1` base-rate samples. `prepare()` allocates; nothing else does.
- `Random.hpp`: `Lcg48Random` reproduces `juce::Random` for the same seed and call pattern;
  `SplitMix64` and `Xoshiro256` are for new code.
- `Fft.hpp`, `Window.hpp`, `LagrangeResampler.hpp`: the real FFT (vendored pffft), the windowing
  tables and the five-point Lagrange resampler. See `fft.md`.
