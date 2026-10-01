# Preset, version 1

Normative. A preset is one JSON file holding a device's parameter values, its state and what
the host adds, in a form any host can read. The format is frozen at 1.0.

Header: `magda/sdk/preset/Preset.hpp` (`readPreset`, `writePreset`, `resolveAssets`).

## Document

```json
{
  "format": "magda.preset",
  "version": 1,
  "id": "3f2b8c1e-9a4d-4e6f-8b7a-1c2d3e4f5a6b",
  "writer": "MAGDA 1.0.0",
  "deviceType": "magdaSampler",
  "deviceVersion": 1,
  "name": "Deep Kick",
  "author": "Ada",
  "tags": ["drums"],
  "created": "2026-10-01T12:30:45Z",
  "valueDomain": "display",
  "parameters": { "magdaSampler_param_0": 0.25 },
  "device": { "kind": "magda", "state": { "schema": 2, "device": "magdaSampler" } },
  "assets": [{ "key": "source", "path": "../Samples/kick.wav", "sha256": "..." }],
  "host": { "magda": { "version": 1 } }
}
```

| Key | Meaning |
|---|---|
| `format`, `version` | `magda.preset` and 1. |
| `id` | 1 to 128 printable ASCII characters, no spaces. Opaque. A UUID for a new preset; a host keeps it across overwrite and rename, and keeps the id of a file it converts. |
| `writer` | Free text naming the writing program. Never read for behaviour. |
| `deviceType`, `deviceVersion` | `DeviceProperties::pluginId` and the manifest's `deviceVersion` the values were written against (docs/parameter-manifest.md). |
| `name` | Non-empty. |
| `author`, `tags` | Optional; omitted when empty. A tag is a non-empty string. |
| `created` | UTC, exactly `YYYY-MM-DDTHH:MM:SSZ`, a real calendar date. |
| `valueDomain` | `display`: the device's real units. `normalized`: positions in [0, 1], for a hosted plugin that has no real units. |
| `parameters` | Object from stable id (docs/parameter-manifest.md) to a finite number, in the `valueDomain`. A parameter that is absent keeps the device's default. |
| `device` | Tagged union, below. |
| `assets` | Optional; omitted when empty. Files the state names by key. |
| `host` | Optional opaque object, below. |

## device

`kind: "magda"` has a `state`: the device state document of docs/device-state.md, whole, with its
own `schema`. Its `device` must equal `deviceType`. A `schema` newer than the reader's makes the
preset a future preset.

`kind: "plugin"` describes a hosted plugin:

| Key | Meaning |
|---|---|
| `format` | `VST3`, `AU`, `LV2`, ... Non-empty. |
| `uniqueId`, `fileOrIdentifier` | How the host finds the plugin. Strings, possibly empty. |
| `vst3ClassId` | Optional. The 32-hex VST3 class id other hosts match on. |
| `chunk` | The plugin's own state, standard base64 with padding; `""` for none. |
| `vst3Preset` | Optional. A Steinberg `.vstpreset`, standard base64. |

## Assets

A state property that names a file holds the asset's `key`, never a path. The entry's `path` is
relative to the preset file, with forward slashes, no leading slash and no empty or `.` segment;
`..` is allowed. `sha256` is 64 lowercase hex digits. Keys are unique.

Resolving is the host's: `resolveAssets` asks a callback about each asset and returns, per key,
`Found`, `Missing` or `Mismatch` (the file exists but its digest differs; the host does the
hashing) with the location the host found. A preset with an unresolved asset still reads; the host
reports it.

## host

One JSON object the SDK keeps and writes back without reading it: member order, number kinds and
nesting survive. Everything else in the document is read strictly; `host` is the one place an
unknown member is accepted. A host puts its own data under its own key and versions it there.
MAGDA uses `host.magda` with its own `version`.

## Reader and writer

- `readPreset` refuses anything `writePreset` would not write: an unknown member outside `host`,
  a duplicate key, a repeated parameter id or asset key, an empty `author`, `tags` or `assets`,
  non-canonical base64, and the shape errors above. Invalid JSON is `NotJson`; a document without
  `format: "magda.preset"` is `NotAPreset`.
- A `version` below 1 is `UnsupportedVersion`. A `version` above 1 is `FutureVersion`, and nothing
  else is read. A host that finds one keeps the file as it is, never replaces it with a preset it
  wrote itself (`isFuturePreset`).
- `writePreset` writes only version 1, members in the order above, two-space indentation, and
  omits optional members that are empty.
