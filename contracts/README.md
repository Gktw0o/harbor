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
