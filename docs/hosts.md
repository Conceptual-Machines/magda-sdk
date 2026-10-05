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

`hosts/juce/MagdaDeviceProcessor` is an `AudioProcessor` over one device:

- Parameters are the slots a fresh instance has, described through `param_descriptor`
  (normalized, with text through the reference conversion under the callback lock). JUCE cannot
  add parameters, so when a state-sourced set changes (`MAGDA_NOTIFY_PARAMETERS_CHANGED`) the
  registered slots are re-described and re-read, a slot past the new count shows as "Unused", and
  the host hears `parameterInfoChanged`.
- MIDI in and out, short and long messages, at their sample positions.
- The play head becomes the transport: block start and end, playing, rendering when non-realtime,
  and a constant tempo when it has a BPM (none otherwise).
- `reset` and `releaseResources` are the ABI's; the tail is the device's, infinite for -1.
- A properties change (the ABI re-prepares the device) reaches the host as its latency; a rate
  change is a new `prepareToPlay`. State the device reports marks the plugin dirty
  (`nonParameterStateChanged`). Notifications come through `magda_host.notify` on the message
  thread, and are taken straight after `prepare` and `set_state`.
- The plugin state document, with the device document written verbatim, so a newer schema's
  text survives a save. Denormals are flushed around `process`.

Buses follow `IS_SYNTH`: a synth has no audio input. An instrument AU must take MIDI, so a synth
sets `NEEDS_MIDI_INPUT TRUE` even when its device ignores MIDI.

With `-DMAGDA_SDK_JUCE_DIR=<a JUCE checkout>`, a top-level build adds
`magda_sdk_juce_host_tests`, the adapter over the reference module.

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