# Device C ABI, version 1

Normative. `magda/sdk/abi/magda_device.h` is the door every host uses to run a device: the JUCE
plugin, the WAM worklet, the parity renderer, and any native host that links a device module. It
wraps `magda::sdk::Device` (docs/device-interface.md) and adds nothing to the device contract.

## Modules

A module is a program that links `magda::sdk_abi` and defines, once,

```cpp
std::span<const magda::sdk::abi::DeviceFactory> magda::sdk::abi::moduleDevices();
```

listing the device types it exposes. `magda_device_type_count` and `magda_device_type_at` read
that list; `magda_device_create` builds one by type. `magda_sdk_add_device_module` in
`cmake/MagdaSdkDeviceModule.cmake` declares a module and its host targets (docs/hosts.md).

## Calls

| Call | Thread | Notes |
|---|---|---|
| `create`, `destroy` | control | `create` is null for an unknown type. |
| `prepare`, `reset`, `latency` | control | `prepare` may follow `prepare`; it releases first. |
| `process` | audio | Planar, in place, any frame count (below). `MAGDA_ERR_STATE` before `prepare`. |
| `set_param` | audio | Normalized, clamped to [0, 1], applied from the next `process`. |
| `get_param`, `param_count` | control | |
| `param_to_real`, `param_to_normalized` | any | The manifest's reference conversion. |
| `midi` | audio | One message at a frame of the next `process` call. |
| `midi_out_count`, `midi_out_at` | audio | What the device emitted in the last `process`. |
| `get_state`, `set_state` | control | Below. |
| `get_manifest` | control | The parameter manifest (docs/parameter-manifest.md). |
| `analyze` | control | Below. |
| `last_error` | control | Why the last failing call on the device failed. |

Control calls are never concurrent with audio calls. Status codes are `MAGDA_OK` and the negative
`MAGDA_ERR_*` values in the header.

## Text

A call that returns text returns a NUL-terminated UTF-8 pointer owned by the device, valid until
the next text-returning call on the same device, or null on failure (`last_error` says why).

## Process

- Any frame count. A call longer than the prepared block size is split into blocks of that size;
  channel pointers advance with it, so a host need not match the device's block size.
- MIDI queued since the last call is sorted by frame (stably) and delivered in the block it
  addresses. A frame past the call's end lands on its last frame. The queue holds 1024 events and
  64 KiB of sysex; `midi` returns `MAGDA_ERR_FULL` past either.
- The device sees MIDI ports only when its properties take, produce or forward MIDI; otherwise
  queued MIDI is dropped.
- MIDI out is rebased to the call's frames and bounded like the input queue.

## State

`get_state` returns the device state document (docs/device-state.md); a new device's is the empty
document of its type. `set_state` takes one:

- A document that decodes, belongs to this device type and that the device restores replaces the
  held document: `MAGDA_OK`.
- Anything else is `MAGDA_ERR_REJECTED` and changes nothing, with the decoder's or the device's
  message in `last_error`.
- A future schema is also `MAGDA_ERR_REJECTED`, but the device loads its defaults and `get_state`
  returns the given text verbatim until a later `set_state` replaces it.

State the device reports through `DeviceHost::stateChanged` is patched onto the held document. A
rebuild the device asks for re-prepares it, with the last prepare's settings, before the control
call that caused it returns.

## Analysis

A device that also implements `magda::sdk::AnalyzingDevice` (`magda/sdk/device/Analysis.hpp`)
answers `analyze` with its own JSON; any other returns null with "the device does not analyze".

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
