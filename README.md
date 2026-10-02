# magda-sdk

Host-independent core of the MAGDA SDK. `core/` holds code that builds natively and to
WebAssembly with no JUCE, DOM or Edit dependency; `hosts/` holds the adapters that may
use them. A configure-time check (`cmake/BoundaryCheck.cmake`) fails on a forbidden
include outside `hosts/`.

## Build

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build --output-on-failure

Wasm (Emscripten, SIMD, no pthreads):

    emcmake cmake -S . -B build-wasm -DCMAKE_CXX_FLAGS=-msimd128
    cmake --build build-wasm

Consume from a parent project with `add_subdirectory` and link `magda::sdk_core`.
Tests are built only when this is the top-level project.

Farbot (`RealtimeObject`, `fifo`) is vendored header-only under `core/third_party/farbot` (MIT,
license kept alongside) rather than reimplemented. Include it as `<farbot/...>`; the boundary
check skips the quoted-include rule there but still forbids JUCE, DOM and Tracktion includes.

The device contract is `core/magda/sdk/device/` (see `docs/device-interface.md`) and its saved
state is `core/magda/sdk/state/` (see `docs/device-state.md`). The portable preset is
`core/magda/sdk/preset/` (see `docs/preset.md`). Curves, with their evaluators, are
`core/magda/sdk/curve/` (see `docs/curve.md`). Waveform peaks are `core/magda/sdk/peaks/` (see
`docs/peaks.md`).

Licensed under MIT.
