# IPC contracts

Harbor's processes talk over per-user named pipes, `\\.\pipe\harbor.<channel>.<sessionId>`.
Every message is one compact UTF-8 JSON object followed by `\n`, at most 64 KiB per line:

```json
{"v":1,"type":"ping","payload":{"seq":42}}
```

- `type` names the message. `v` is that message's version.
- Schemas here are the source of truth and are strict (`additionalProperties: false`).
  All Harbor executables ship together, so there is no cross-version compatibility to keep.
- **Any change to a message bumps its `v`** and updates both the C++ and C# sides and their
  contract tests in the same change.
- Receivers validate, log and drop anything that doesn't match. Bad input never crashes a process.
- Duplicate keys are rejected by both decoders. JSON Schema can't express that rule, so it is covered by
  unit tests on each side (`RejectsDuplicateKeys`) rather than by fixtures.

## Fixtures

`fixtures/valid/*.json` must validate against their message's schema, decode, and re-encode to equal JSON.
`fixtures/invalid/*.json` must match no message schema and be rejected by both decoders.

- `tests/dock` checks every fixture against the schemas (JSON Schema draft-07, via
  [json-schema-validator](https://github.com/pboettch/json-schema-validator)) and the C++ codec.
- `tests/Harbor.Core.Tests` checks every fixture against the C# codec.

Both suites list the files when they run, so a new schema or fixture is picked up without a rebuild.

| Message | Direction | Channel |
|---|---|---|
| [`ping`](ping.schema.json) v1 | Shell → dock | `control` |
| [`pong`](pong.schema.json) v1 | dock → Shell | `control` |
| [`setTaskbar`](setTaskbar.schema.json) v1 | Shell → dock | `control` |
| [`taskbarState`](taskbarState.schema.json) v1 | dock → Shell | `control` |
| [`dockItems`](dockItems.schema.json) v1 | Shell → dock | `control` |
| [`appLookup`](appLookup.schema.json) v1 | dock → Shell | `control` |
| [`appInfo`](appInfo.schema.json) v1 | Shell → dock | `control` |
| [`pinRequest`](pinRequest.schema.json) v1 | dock → Shell | `control` |

The running-app messages (docs/adr/0010): the dock sends `appLookup` once for each app it sees running (and
again for every live one after a reconnect), naming it by a number of its own; the Shell answers with `appInfo`
for that number, possibly twice (at once, then with the real name and icon). `pinRequest` asks the Shell to pin
or unpin; the dock changes nothing until the next `dockItems`. All three are far below the line limit at their
bounds.

Text goes on the wire as UTF-8; only `"`, `\` and control characters are escaped. `dockItems` is bounded so its
largest form ([`dock-items-largest.json`](fixtures/valid/dock-items-largest.json), 64,888 bytes) fits one
line; both test suites check that, and that their encoders write it byte for byte.

## Icon files

`dockItems` names each item's icon by a file in `%LOCALAPPDATA%\Harbor\icons\` (`<data dir>\icons`). The
Shell makes them; the dock only reads them. Raw pixels rather than PNG, so the dock needs no image decoder.

- **Name:** the first 16 lowercase hex digits of the SHA-256 of the file's bytes, then `.bgra`. A file never
  changes once written; a changed icon gets a new name.
- **Layout:** the 4 ASCII bytes `HIC1`, the width and the height (each a little-endian uint32, 1 to 256),
  then width × height pixels, rows top to bottom with no padding, 4 bytes each: blue, green, red, alpha, with
  the colors premultiplied by alpha (each ≤ alpha). The file is exactly 12 + 4 × width × height bytes.
- **Writing:** the Shell writes a temporary file in the same folder and renames it, so a reader never sees
  half a file. A file that doesn't match the layout is treated as no icon.
- **Fixtures:** `icons/fixtures/{valid,invalid}/*.bgra`. Harbor.Core.Tests writes the valid ones byte for
  byte; tests/dock parses them and refuses every invalid one. `*.bgra` is binary in `.gitattributes`.

## State files

Files that more than one Harbor process reads live in `state/`, with the same rules: a strict schema, a
`v` that any change bumps, and fixtures in `state/fixtures/<name>/{valid,invalid}` that both test suites
check against their decoders (and `tests/dock` against the schema).

| File | Schema | Written by | Read by |
|---|---|---|---|
| `%LOCALAPPDATA%\Harbor\taskbar-restore.json` | [`taskbar-restore`](state/taskbar-restore.schema.json) v1 | dock, before it turns on auto-hide | dock and Shell, to put auto-hide back |
| `%LOCALAPPDATA%\Harbor\settings.json` | [`settings`](state/settings.schema.json) v1 | Shell (defaults on first run) and people | Shell only (`tests/dock` checks the schema alone) |
