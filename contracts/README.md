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
Both test suites (`tests/dock`, `tests/Harbor.Core.Tests`) run every fixture, so a new file here is
picked up by both sides automatically.

| Message | Direction | Channel |
|---|---|---|
| [`ping`](ping.schema.json) v1 | Shell → dock | `control` |
| [`pong`](pong.schema.json) v1 | dock → Shell | `control` |
