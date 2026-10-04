# Device C ABI, version 1

Normative. `magda/sdk/abi/magda_device.h` is the door every host uses to run a device: the JUCE
plugin, the WAM worklet, the parity renderer, and any native host that links or loads a device
module. It wraps `magda::sdk::Device` (docs/device-interface.md) and adds nothing to the device
contract.

## Modules

A module is a program that links `magda::sdk_abi` and defines, once,

```cpp
std::span<const magda::sdk::abi::DeviceFactory> magda::sdk::abi::moduleDevices();
```

listing the device types it exposes. `magda_sdk_add_device_module` in
`cmake/MagdaSdkDeviceModule.cmake` declares a module and its host targets (docs/hosts.md).
`examples/gain` is the smallest one.

## Entry

The module exports one symbol:

```c
const magda_module* magda_module_entry(int32_t host_abi_version);
```

The host passes the `MAGDA_ABI_VERSION` it was built against. A module serves only its own ABI
version and returns null for any other. The table it returns is static and valid for the life of
the module:

| Field | Meaning |
|---|---|
| `abi_version` | `MAGDA_ABI_VERSION`, 1. |
| `state_schema` | The device state schema the module reads and writes (docs/device-state.md), 2. |
| `sdk_version` | The SDK release the module was built with, `major.minor.patch`. |
| `device_types`, `device_type_count` | The types, in `moduleDevices()` order. |
| `api` | The device functions. |
| `get_extension` | Reserved for later surfaces (analysis, telemetry); null for every id in version 1. |

Each `magda_device_type` carries the type `id` (also the state document's `device`), a display
`name`, and the type-level parameter manifest (docs/parameter-manifest.md) with its byte size.
The manifest is built without a host and lists the parameters a fresh instance has; a device whose
parameters come from its state lists its generic slots.

## Structs

- A tagged struct opens with `struct_tag`, its `MAGDA_TAG_*` value, and `struct_size`, the size
  the writer knows. Version 1 is the first: a size below the version 1 struct is refused with
  `MAGDA_ERR_ARGUMENT`, a larger one is read up to the size the module knows. Later versions
  append fields, and a field past a struct's size reads as zero, which every appended field takes
  as its default.
- For a struct the device fills (`magda_properties`), the host sets the tag and size and the
  device writes the fields that fit.
- Array elements (`magda_device_type`, `magda_midi_event`, `magda_param_segment`) carry no tag.
  Their layouts are frozen; the `reserved` fields are zero and are where a later version adds.
- Status is `int32_t`: `MAGDA_OK` or a negative `MAGDA_ERR_*`.

## Threads

| Call | Thread |
|---|---|
| `create`, `destroy`, `get_properties`, `prepare`, `release`, `reset`, `latency` | control |
| `param_count`, `param_offered`, `param_descriptor`, `param_to_real`, `param_to_normalized`, `get_manifest` | control |
| `get_state`, `set_state`, `take_notifications`, `take_state_patch`, `last_error` | control |
| `process`, `set_param`, `set_param_segments` | audio |
| `param_value`, `tail` | control, and safe while `process` runs |

- Control calls are made one at a time and never concurrently with audio calls. In a
  single-threaded host, control means serialized with `process`.
- Parameter writes are audio calls, made before the `process` they apply to, in block order. A
  host queues writes that originate on its control thread and makes them on the audio thread.
- `reset` is a control call.

## Lifecycle

1. `create(type, host, &device)`: `MAGDA_ERR_ARGUMENT` for an unknown type or a malformed host
   struct, with `*device` null.
2. `get_properties`: constant until the next `prepare`.
3. `prepare`: `sample_rate`, `max_frames`, and `max_midi_events` (0 means 1024), the most MIDI
   events one `process` call carries. A `prepare` may follow another; it releases first.
4. `process` any number of times.
5. `release`, after which `prepare` may follow again; `destroy` releases a prepared device.

`latency` is valid after `prepare` and 0 before. `tail` is samples of ring-out, or -1 for a tail
that never decays.

## Properties and channels

`magda_properties.flags` holds `MAGDA_PROPERTY_*`: whether the device takes, produces or forwards
MIDI, takes audio, is a synth, produces audio without input, keys on a MIDI sidechain, and
whether its parameters come from its state. `input_channels` is what it reads (0: the host
decides), `output_channels` what it always writes (0: follows the input), `sidechain_channels`
its audio key (0: none), `device_version` the manifest's.

Audio is in place, as in `Device`: the host passes `max(input, output)` channels, or its own
count when both are 0, holding the input on entry and the output on exit.

## Process

`magda_process` carries one block:

- `num_frames`, at most the prepared `max_frames`; more is `MAGDA_ERR_ARGUMENT`. Zero is a no-op
  that still empties MIDI out. Before `prepare`, `MAGDA_ERR_STATE`.
- `channels`, planar, `num_channels` of them.
- `sidechain`, read-only, `sidechain_channels` of them; null is no key, not a silent one.
- `midi_in` and `midi_out`, both set or both null. Input events are in block order (a later event
  never has an earlier sample) and are at most `max_midi_events`, or the call fails with
  `MAGDA_ERR_ARGUMENT` or `MAGDA_ERR_FULL`. A sample past the block lands on its last frame.
  `MAGDA_MIDI_ALL_NOTES_OFF` in `flags` is the host's panic.
- Output goes into the host's storage: `events` up to `capacity`, the bytes of long messages into
  `bytes` up to `byte_capacity`. The call resets `count`, `bytes_used` and `flags`; an event that
  does not fit is dropped, which the device sees as `addEvent` returning false. A device that
  routes no MIDI sees neither port.
- `transport`, or null for none: block start and end in seconds, `MAGDA_TRANSPORT_PLAYING` and
  `MAGDA_TRANSPORT_RENDERING`, and the tempo: `MAGDA_TEMPO_NONE`, `MAGDA_TEMPO_CONSTANT` at
  `bpm` with beat 0 at timeline zero, or `MAGDA_TEMPO_MAP` through `tempo_map`, whose functions
  the device calls on the audio thread during the call. None is distinct from a constant tempo.
- `live_source_ids`, the MIDI sources the host counts as live; empty means none is known live.

`process` allocates nothing. It never touches `last_error`.

## Parameters

Slots are `[0, param_count)`, values normalized to [0, 1]. `set_param` clamps and applies from
the next `process`; `set_param_segments` hands sample-accurate segments (at most `max_frames`,
after `prepare`) to the device's segment setter. `param_offered` says whether a slot is in use
now. `param_descriptor` writes one parameter as its manifest entry; `get_manifest` the instance's
whole manifest, which differs from the type's when parameters come from state.
`param_to_real` and `param_to_normalized` are the manifest's reference conversion
(`normalizedToReal`, `realToNormalized`).

## Text

Every call that returns text (`param_descriptor`, `get_manifest`, `get_state`,
`take_state_patch`) takes `(buffer, capacity, size)`. It writes the length without the terminator
to `*size`; when `capacity` is at least `*size + 1` it writes the text and a NUL, otherwise it
returns `MAGDA_ERR_BUFFER` and writes nothing. Calling with a null buffer asks for the size; the
text does not change before the next control call.

## State

- `get_state` returns the device state document. Before any `set_state` it is the bare
  document, `{"schema": 2, "device": "<type>"}`.
- `set_state` with a document that decodes, belongs to this device type and that the device
  restores replaces the held document: `MAGDA_OK`. Anything else is `MAGDA_ERR_REJECTED`, changes
  nothing, and leaves the held document as it was.
- A newer schema is `MAGDA_ERR_REJECTED` too, but the device loads its defaults and `get_state`
  returns the given text verbatim until a later `set_state` replaces it. Patches the device
  reports meanwhile do not touch it.

## Notifications

Flags accumulate until `take_notifications` returns and clears them:

| Flag | Raised when |
|---|---|
| `MAGDA_NOTIFY_STATE_CHANGED` | The device reported state of its own (`DeviceHost::stateChanged`). The held document already carries it; `take_state_patch` returns the patches merged since the last take, as a state document, and `MAGDA_ERR_STATE` when none is pending. A successful `set_state` drops a pending patch. |
| `MAGDA_NOTIFY_PARAMETERS_CHANGED` | A `set_state` was adopted (or a newer schema loaded defaults) on a device whose parameters come from its state: re-read the count, offered slots and descriptors. |
| `MAGDA_NOTIFY_PROPERTIES_CHANGED` | The device asked for a rebuild. The ABI re-prepares it with the last `prepare`'s settings before the control call that caused it returns, or at the start of the next control call when it was asked outside one: re-read properties and latency. |

A host may also pass `magda_host.notify` at `create`. It is called on the control thread when new
flags are pending, after the control call that raised them has done its work, and never inside
`process` or `set_state`: flags raised by `set_state` are announced at the next control call, so
a host that wants them at once takes them after `set_state`. The callback may make control calls.

## Errors

`last_error` describes the last control call: empty when it succeeded, the reason when it failed,
valid until the next control call. Audio calls report through their status only.

## Denormals

Host adapters flush denormals around `process` (the JUCE adapter with `ScopedNoDenormals`).
Devices must not rely on it.

## Not in version 1

Telemetry, presets and analysis stay C++ (`Device::telemetry`, docs/preset.md,
`magda/sdk/device/Analysis.hpp`); `get_extension` is where they arrive.

## Host helpers

`magda/sdk/abi/AbiDevice.hpp` is a header-only C++ wrapper over the table that hosts share: MIDI
queued ahead of `process`, calls of any length split into prepared blocks with MIDI out rebased,
and text read into `std::string`. `magda/sdk/abi/AbiHarness.hpp`'s
`checkModuleConformance(module)` runs every type of a module through the rules above that hold
for any device and returns what it broke.

## Plugin state

Parameters are not device state, so a plugin host saves both, as one JSON document that the JUCE
and WAM hosts read and write alike:

```json
{
  "format": "magda.plugin-state",
  "version": 1,
  "parameters": { "frequency": 0.375 },
  "state": { "schema": 2, "device": "toneGenerator" }
}
```

`parameters` maps stable ids to normalized values; `state` is the device state document.
