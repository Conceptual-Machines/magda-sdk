# Device state, schema 2

Normative. A device's saved state is one JSON document. The host owns the document and
a device restores it (`Device::restoreState`); there is no flush. State a device produces
on its own reaches the host through `DeviceHost::stateChanged`.

Headers: `magda/sdk/state/StateNode.hpp`, `StateCodec.hpp`, `BinaryText.hpp`.

## Document

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["schema", "device"],
  "additionalProperties": false,
  "properties": {
    "schema":   { "const": 2 },
    "device":   { "type": "string", "minLength": 1 },
    "props":    { "$ref": "#/$defs/props" },
    "children": { "type": "array", "items": { "$ref": "#/$defs/child" } }
  },
  "$defs": {
    "value": {
      "oneOf": [
        { "type": "integer", "minimum": -9223372036854775808, "maximum": 9223372036854775807 },
        { "type": "number" },
        { "type": "boolean" },
        { "type": "string" },
        { "$ref": "#/$defs/bin" }
      ]
    },
    "bin": {
      "type": "object",
      "required": ["$bin"],
      "additionalProperties": false,
      "properties": { "$bin": { "type": "string" } }
    },
    "props": {
      "type": "object",
      "propertyNames": { "minLength": 1 },
      "additionalProperties": { "$ref": "#/$defs/value" }
    },
    "child": {
      "type": "object",
      "required": ["type"],
      "additionalProperties": false,
      "properties": {
        "type":     { "type": "string", "minLength": 1 },
        "props":    { "$ref": "#/$defs/props" },
        "children": { "type": "array", "items": { "$ref": "#/$defs/child" } }
      }
    }
  }
}
```

`schema` and `device` are the names the files already carry; nothing else is renamed. The
document type has no parameter record: automatable parameters are not state.

## Nodes and properties

- A node has a type (empty for the root), ordered properties with unique non-empty keys,
  and ordered children. Insertion order survives a round trip.
- Setting an existing key replaces its value in place. Removing a key keeps the order of the
  rest.
- A value is int64, double, bool, string or binary. A double is finite: JSON cannot spell
  anything else, and `StateNode::setDouble` refuses it.
- A number token with no fraction and no exponent that fits int64 is an int64; any other
  number is a double. The writer always spells a double with a `.` or an exponent, so the
  kind survives.
- Nodes nest at most 64 deep, the root being depth 1.

## Reading a value as another kind

A kind mismatch is never a read failure. The getters coerce exactly as `juce::var` does:

| From \ To | int / int64 | double | bool | string |
|---|---|---|---|---|
| int64 | int saturates | exact | `!= 0` | decimal |
| double | truncate toward zero, saturating | itself | `!= 0` | shortest text that reads back |
| bool | 0 or 1 | 0.0 or 1.0 | itself | `"1"` or `"0"` |
| string | leading blanks, optional sign, digits up to the first other char; both saturate (`"60"` 60, `"1.5"` 1, `"+5"` 5, `"abc"` 0) | leading blanks, sign, digits, point, exponent (`"1.5"` 1.5, `"+2"` 2.0, `"abc"` 0.0) | int value `!= 0`, or trimmed `true` / `yes` ignoring case | itself |
| binary | 0 | 0.0 | false | the `$bin` text |

- The fallback passed to a getter applies only to an absent key.
- A string that coerces to a non-finite double (`"nan"`, `"inf"`, `"1e999"`) reads as the
  fallback, so a getter never returns a non-finite value.
- Binary is never produced from another kind: `getBinary` is null unless the value is binary.
- Blanks are ASCII whitespace.

## Binary

A binary value is the object `{"$bin": "<text>"}` and nothing else. The text is
`<count>.<chars>`:

- `<count>` is the byte count in decimal, without leading zeros (`0` alone for none).
- `<chars>` is `ceil(8 * count / 6)` chars of the alphabet `.A-Za-z0-9+`
  (`.` is 0, `A` to `Z` are 1 to 26, `a` to `z` are 27 to 52, `0` to `9` are 53 to 62, `+` is 63).
- Char `i` carries bits `6i` to `6i + 5` of the byte stream, least significant bit first within
  each byte and within the char. Padding bits are zero.

This is byte for byte the output of `juce::MemoryBlock::toBase64Encoding`. The decoder is
stricter than JUCE's: it refuses a missing or non-canonical count, a char outside the alphabet,
the wrong char count, nonzero padding, and a count over 256 MiB.

## Canonical decoder

`decodeDocument` accepts exactly what `encodeDocument` writes, and says which of these it hit:

| Status | Meaning |
|---|---|
| `NotJson` | Not RFC 8259 JSON. Also: duplicate object keys, invalid UTF-8, lone surrogate escapes, numbers outside the grammar or outside double range, nesting past the limit. |
| `NotADocument` | JSON, but without `schema` and `device`. |
| `UnsupportedSchema` | `schema` below 2. |
| `FutureSchema` | `schema` above 2. Nothing else is read. |
| `Invalid` | Schema 2 with content the form does not allow: a non-integer `schema`, an empty `device`, an unknown member (`params` included), a `props` that is not an object, an empty property key, a property value that is an array, `null`, or any object other than a valid `$bin`, a child without a non-empty `type`, a malformed `$bin`. |

## Canonical writer

`encodeDocument` writes only schema 2, with `schema`, `device`, then `props` and `children` when
non-empty, children as `type`, `props`, `children`. It refuses, and writes nothing, for a
schema other than 2, an empty device type or property key, a typed root, an empty child type,
invalid UTF-8, or nesting past the limit.

## Future schemas

A reader that cannot read a document must leave it alone. `isFutureSchema(text)` is true when
`text` is a JSON object with `schema` and `device` and a `schema` above the one this build
reads. A host that finds it keeps the saved text, loads the device's defaults, and never
replaces the text with a document it wrote itself, or opening and saving would downgrade it.

## Hosts and older files

The tolerant legacy readers (engine XML, the pre-2317 `params` record, engine-owned props and
children, retired device-type aliases) belong to the host. They normalise into this document
before a device sees it.
