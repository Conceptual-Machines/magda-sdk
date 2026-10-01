# Parameter manifest

Normative. A manifest describes one device's parameters without instantiating it: what a host
shows, stores, automates and maps. It is generated at build time from
`Device::parameterDescriptor` (`magda/sdk/device/ParameterDescriptor.hpp`), written and read by
`magda/sdk/device/ParameterManifest.hpp`. The format is frozen at 1.0.

## Document

```json
{
  "format": "magda.device-manifest",
  "version": 1,
  "deviceType": "magda_delay",
  "deviceVersion": 1,
  "parameterSource": "static",
  "parameters": []
}
```

- `deviceType` is `DeviceProperties::pluginId`. `deviceVersion` is a positive integer, 1 for a
  device's first release; bump it when its parameter set changes.
- `parameterSource` is `static` when the device fixes its parameters, `state` when its saved
  state decides them (runtime Faust devices). A `state` manifest lists the generic slots the
  device always has; the state names the rest.
- The reader refuses an unknown key anywhere, a duplicate key, an unsupported `version`, a
  repeated `id` or `index`, and any key a scale kind does not take. The writer refuses what the
  reader would.

## Parameter

| Key | Type | Meaning |
|---|---|---|
| `id` | string | Stable id, unique per device, the cross-host key. Required, non-empty. |
| `index` | integer | The frozen slot, unique, at least 0. Saved automation, macro and mod links address it. |
| `name` | string | Display name. |
| `unit` | string | `Hz`, `ms`, `dB`, ... Omitted when empty. |
| `group`, `tooltip` | string | UI page and hover text. Omitted when empty. |
| `widthCells` | integer | Grid cells asked for, at least 1. Advisory. Omitted at 1. |
| `min`, `max` | number | Range in real units, `min <= max`. |
| `default` | number | In the domain `valueConvention` names. |
| `valueConvention` | `real` \| `normalized` | Domain of `default`. Omitted at `real`. |
| `scale` | object | How a normalized position maps to a real value; see below. |
| `step` | number | Quantisation in real units; 0 or omitted is continuous. |
| `displayFormat` | string | `default` (dispatches on `unit`), `decibels`, `pan`, `percent`, `midiNote`, `beats`, `barsBeats`. Omitted at `default`. |
| `labelTicks` | array | `{value, label}` pairs for the automation axis, at real values. |
| `gate` | object | `{slot, negated?}`: the parameter is dimmed unless slot `slot` is on (off when `negated`). On means normalized at least 0.5. |
| `hidden` | bool | Addressable but left out of grids. |
| `momentary` | bool | A boolean that is 1 while pressed. |
| `automatable` | bool | Default true. |
| `readOnly` | bool | Default false. |
| `modulatable` | bool | Default true. |
| `bipolarModulation` | bool | Default false. |
| `wrapperRole` | `none` \| `dryGain` \| `wetGain` | Default `none`. |

Booleans are written only when they differ from the default. Numbers are JSON numbers; a float is
written as the shortest text that reads back as the same float.

## Scale

`scale.kind` is one of six, and each takes exactly its own keys. `n` is the normalized position
in [0, 1], clamped.

| Kind | Keys | Real value |
|---|---|---|
| `linear` | `anchor?` | `min + n * (max - min)` |
| `logarithmic` | `anchor?` | `min * (max / min)^n`; linear when `min <= 0` |
| `exponential` | `exponent` | `min + (max - min) * n^exponent` |
| `discrete` | `choices`, `radio?` | the position of the choice in `choices`, 0 when empty |
| `boolean` | | 1 when `n >= 0.5`, else 0 |
| `faderDb` | `unityPosition`, `unityDb` | `min` at 0, `unityDb` at `unityPosition`, `max` at 1, linear between |

- `anchor` is the real value placed at `n = 0.5`. It counts only when strictly inside
  `(min, max)`, and the reader refuses one that is not; omit it for none. The curve is the one
  the reference functions below compute.
- `unityPosition` is in `(0, 1)`; devices use 0.75 and 0 dB.
- A `choices` entry is `{id, label, value}`: `id` is non-empty and unique within the parameter,
  `label` is shown, `value` is what the choice stands for in the underlying plugin (a Faust menu
  value), a number. Choice order is the parameter's order and its real values.
  `radio` asks for buttons rather than a dropdown, and the UI may ignore it.
- Descriptor fields a kind does not take (an exponent on a linear parameter, choices on a
  boolean) are not written.

## Reference conversion

`magda/sdk/device/ParameterDescriptor.hpp` ships `normalizedToReal` and `realToNormalized` for
every kind over a `ParameterDomain`, the flat subset of a descriptor the curve reads
(`domainOf(descriptor)`). Every host maps through these and no other implementation; they are
float arithmetic and allocation free, so the audio thread may call them.

## Ids and indices

A device may leave `stableId` empty, and `index` negative. The manifest writes them resolved:

- `index` is the declared index, or the slot when it is negative;
- `id` is the declared id, or `<pluginId>_param_<index>` when it is empty.

`buildManifest(device)` applies this rule (`resolveDescriptor`) and the writer refuses a manifest
that is not resolved. A parameter keeps its id and index for the life of the device; a renumbered
slot is a new `deviceVersion` and a break for saved automation.
