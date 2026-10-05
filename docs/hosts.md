# Hosts

Everything under `hosts/` runs a device through the C ABI (docs/abi.md). It is the only part of
the repo allowed to depend on JUCE or the browser.

## Device modules

```cmake
magda_sdk_add_device_module(<name> SOURCES <files> [LIBRARIES <targets>])
```

`<name>_module` is an OBJECT library: the sources that define `moduleDevices()` and whatever
they link. Natively the call also adds:

- `<name>_render`, the parity renderer (below);
- `<name>_manifests --out <dir> [--vendor V] [--prefix P] [--version X]`, which writes
  `<type>.manifest.json` and the WAM `<type>.descriptor.json` for every device in the module.

Under Emscripten it adds `<name>_wasm`, written as `<name>.wasm`: a standalone reactor with no
entry point, importing only `env.emscripten_notify_memory_growth`. It exports
`magda_module_entry`, `malloc`, `free`, and the flat `magda_wam_*` glue in `hosts/wam/reactor.cpp`,
which builds the ABI structs in C++ so JavaScript never lays one out.

## JUCE

```cmake
magda_sdk_add_juce_plugin(<target> MODULE <name> DEVICE <type> [EXCLUDE_FROM_ALL]
                          <juce_add_plugin arguments>)
```

`hosts/juce/MagdaDeviceProcessor` is an `AudioProcessor` over one device: parameters from the
manifest (normalized, with text through the reference conversion), MIDI in and out, the plugin
state document, and a generic editor. Buses follow `IS_SYNTH`: a synth has no audio input. An
instrument AU must take MIDI, so a synth sets `NEEDS_MIDI_INPUT TRUE` even when its device
ignores MIDI.

### Display lists

`hosts/juce/DisplayListGraphics` draws a display list (docs/display-list.md) into a
`juce::Graphics`, resolving each colour role through a function the shell supplies, read at
paint time, and text through an optional font function. `magda_sdk_target_juce_display(<target>)` adds it to a JUCE target. Clips snap
outward to whole pixels.

## WAM 2

`hosts/wam/js` is the npm package `@conceptual-machines/magda-sdk-wam`:

- `getMagdaDeviceAbi` binds the module's glue synchronously, so it also runs in an
  AudioWorklet; its audio path allocates nothing.
- `getMagdaWamProcessor` is the `WamProcessor`: parameters from the manifest (booleans and
  choices as WAM booleans and choices, the rest normalized floats), MIDI and sysex from the WAM
  event queue, emitted MIDI back onto it, and the plugin state document.
- `createMagdaWam({ wasmUrl, descriptorUrl, deviceType })` returns the `WebAudioModule` class for
  a device. The worklet cannot fetch, so the main thread loads the wasm bytes and hands them to
  the processor.

`hosts/wam/js/demo/index.html` is a one-plugin WAM host. With `?autocheck` it renders offline
through the WAM and through the ABI directly and reports whether they match.

## Parity

```
node hosts/parity/parity.mjs --wasm <name>.wasm --render <name>_render --corpus <corpus.json>
                             [--tolerance 1e-5]
```

renders every case natively and through the wasm module in node and fails on any sample that
differs by more than the tolerance, or on a silent render unless the case sets `expectSilence`.
A corpus is `{"format": "magda.parity-corpus", "version": 1, "cases": [...]}`; a case names
`device`, and optionally `sampleRate`, `blockSize`, `channels`, `frames`, `parameters` (stable id
to normalized value), `state` (a device state document), `midi` (`{sample, bytes}`), and `input`
(`silence`, `impulse` or `sine` at `inputFrequency`). `hosts/parity/reference-corpus.json` runs
the reference module in this repo's CI.

## Canvas 2D

`hosts/canvas` is the npm package `@conceptual-machines/magda-sdk-canvas`: `drawDisplayList(ctx,
list, palette, fontFamily)` draws a parsed display list, with `palette` mapping role names to ARGB
numbers (`defaultPalette` for the rest).

Under Emscripten the top-level build adds `magda_sdk_ui_demo.wasm`: the meter, the curve editor
and the waveform view behind a few C exports (`hosts/canvas/ui_demo.cpp`), importing
`env.measure_text` for the editor's tooltip. `node hosts/canvas/check.mjs --wasm <it>` runs
every golden through it. `hosts/canvas/demo/`, served from the repo root, has a page per core:
the golden cases and a live meter, editor or waveform pane. With `?autocheck` a page also probes
pixels (and the editor page drives a drag and its undo through the live shell) and reports in
`#result`.