# Device interface

Normative. `magda::sdk::Device` (`magda/sdk/device/Device.hpp`) is the contract a device
implements and a host runs. It names no JUCE, DOM or Tracktion type, strings are `std::string`,
and nothing in `process()` allocates.

## Threads

| Call | Thread |
|---|---|
| `setHost`, `properties`, `prepare`, `release`, `reset`, `latencySamples`, `tailSamples`, `parameterCount`, `parameterDescriptor`, `offersParameter`, `parameterValue`, `restoreState`, `telemetry` | control |
| `process`, `setParameterValue`, `setParameterSegments` | audio |
| every `DeviceHost` call | control |

- Control calls are made one at a time and never concurrently with `process()`, except
  `tailSamples`, `parameterValue` and `telemetry`, which may run while the device renders and
  must be safe to (atomics for what the audio thread writes).
- A device that wants to tell the host something from the audio thread queues it and sends it
  from the control thread.
- The host that owns the device outlives its attachment: `setHost(host)` before `prepare`,
  `setHost(nullptr)` before the device is destroyed.

## Lifecycle

1. `setHost`.
2. `properties()` is read. It is constant until the next prepare; to change it, call
   `DeviceHost::rebuildRequired()` and the host re-prepares.
3. `prepare(context)`: allocate everything `process()` will use, for `sampleRate` and
   `maximumBlockSize`. `latencySamples()` is read after it.
4. `process()` any number of times, blocks of at most `maximumBlockSize` frames.
5. `release()`. `prepare` may follow again.

`reset()` drops history (delay lines, voices, tails) without changing parameters or state.

## Latency and tail

- Latency is a sample count, read after prepare.
- Tail is a sample count of how long the output rings after the input stops, or
  `kInfiniteTail`. It may change between prepares (a loaded impulse, a release time); the host
  reads it when it needs it. A render that asks for the tail treats infinite as none.

## Process context

`ProcessContext` is valid for the process call only.

- `audio` is a `BufferView` (`magda/sdk/audio/BufferView.hpp`) over the host's channel
  pointers, already sliced to the block: frame 0 is the block's first frame, and there is no
  channel cap. The device reads its input and writes its output in place.
- `sidechain` is absent when nothing is routed, which is "no key" and not a silent one. It is
  read-only, and may have fewer channels than the device declared.
- `midiIn` and `midiOut` are both null when the host routed no MIDI, otherwise both set.
- `tempoMap` answers `beatsAtSeconds` and `bpmAtSeconds`; null when the host has none.
- `timelineStartSeconds`, `timelineEndSeconds`, `isPlaying`, `isRendering` describe the block.
  `isRendering` is an offline render: skip live-only work.
- `liveSourceIds` lists the sources the host counts as live input, against `MidiEvent::sourceId`.
  Empty means none is known live, never all of them.

## MIDI

An event is a short message (up to three bytes, inline) or a long one (sysex, a pointer and a
length), an `int32` sample from the start of the block, a float fraction of that sample in
[0, 1), and a `sourceId`.

- Input is read-only and in block order. A long message points into host storage valid for the
  process call. `isAllNotesOff()` is the host's panic, raised without a controller event.
- Output starts empty; on exit it is the device's whole output. `addEvent` copies the event
  (a long message's bytes too) and returns false, adding nothing, once the port's event count or
  byte budget is spent. An event outside the block lands on the nearest sample the block has.
- Whether input continues past the device is the host's routing (thru), never the device's.
  `DeviceProperties::producesMidi` says the device emits MIDI of its own;
  `forwardsMidiInput` says it copies part of its input onto its output, so the host sizes the
  output port for both.
- `midiEventPosition(seconds, sampleRate)` is the rule for placing a time on a sample.

## Parameters

Parameters are addressed by slot, normalized to [0, 1]. Before each `process()` the host calls
`setParameterValue(slot, value)` with the block's value, or `setParameterSegments(slot,
segments)` for a sample-accurate value: linear spans from each segment's start value to its end
value, ending where the next begins. The default segment form applies the first segment's start
value through the scalar setter. `parameterDescriptor(slot)` returns the slot's static description, a
`ParameterDescriptor`; the parameter manifest (`docs/parameter-manifest.md`) is built from it.

## State

- The host owns the state document; the device restores it. `restoreState(node)` receives the
  document's root, never a parameter value, and returns ok or an error with a message.
- Restore is transactional: on error the device keeps the state it had.
- Absent properties and children read as the device's defaults; unknown ones are ignored.
- State a device produces and the host's document does not hold (a UI toggle that lives on the
  device) goes to `DeviceHost::stateChanged(node)`: the node's properties are written onto the
  document root and its children replace the root's children of the same types. It is not called
  from `restoreState`.

## Telemetry

A device exposes display data through `telemetry(key)`: a `DeviceTelemetry` it owns for its
lifetime. `SampleRingTelemetry` (a mono ring filled on the audio thread, read by one other
thread), `AudioTapTelemetry` and `LevelsTelemetry` are in `magda/sdk/telemetry/Telemetry.hpp`.
