# FFT

`magda/sdk/dsp/Fft.hpp` is the one FFT interface in the SDK; `Window.hpp` and
`LagrangeResampler.hpp` sit beside it. They are the JUCE-free counterparts of `juce::dsp::FFT`,
`juce::dsp::WindowingFunction` and `juce::LagrangeInterpolator` (#2936).

## Interface

- `RealFft`: real forward and inverse transform of one power-of-two size, 32 to 2^24.
  `prepare(size)` allocates; `forward`, `forwardMagnitude` and `inverse` do not, so they are
  safe on the audio thread. One instance per thread.
- A spectrum is `size / 2 + 1` bins as interleaved `(re, im)` floats. Forward is the plain
  unscaled DFT, which is what `juce::dsp::FFT::performRealOnlyForwardTransform` returns on
  every JUCE backend; inverse divides by `size`, so `inverse(forward(x))` is `x`.
- `Window` / `fillWindow`: rectangular, triangular, Hann, Hamming, Blackman, Blackman-Harris,
  flat top and Kaiser, evaluated in float in the same order as
  `juce::dsp::WindowingFunction<float>`, including the symmetric `size - 1` denominator and
  the `normalise` flag (scales the table to mean one). Hann tables are bit-identical on the
  same libm.
- `LagrangeResampler`: the five-point Lagrange interpolator, the same algorithm as
  `juce::LagrangeInterpolator`.

## The library: pffft

Vendored in `core/third_party/pffft/` from the maintained fork
(github.com/marton78/pffft, revision `aa16fd3`), trimmed to the float transform: `pffft.c`,
`pffft_common.c`, `pffft_priv_impl.h`, `include/pffft/pffft.h` and the float SIMD headers.
No source changes. Its licence (the FFTPACK licence, BSD-style, permissive) is in
`core/third_party/pffft/LICENSE.txt` and is compatible with the SDK's MIT.

Chosen over kissfft for the SIMD paths. pffft has SSE, NEON, AltiVec and, in this fork, WASM
simd128 (`pf_wasm_float.h`, switched on by `PFFFT_ENABLE_WASM` when building with Emscripten
and `-msimd128`), with a scalar fallback everywhere else. kissfft is scalar on those targets
and measurably slower; its only advantage is accepting non-power-of-two sizes, which nothing
here uses. Costs of pffft: float only, power-of-two real sizes of at least 32 with SIMD, and
16-byte aligned buffers, which `RealFft` hides with one copy into an aligned scratch buffer.

The WASM path is compiled for `wasm32` with simd128 in a local clang check but not run here;
the SDK wasm CI job builds it.

## Against the juce versions

FFT libraries round differently, so `RealFft` is checked against a naive double-precision DFT
(1e-5 of the largest bin) and by round trip, not against bit patterns. A float FFT's error
floor follows the loudest bin, so a consumer comparing spectra should bound the difference
by a fraction of the frame's peak rather than of the bin read. `Window` tables follow the
closed forms, and `LagrangeResampler` runs the same float arithmetic in the same order as
`juce::LagrangeInterpolator`.
