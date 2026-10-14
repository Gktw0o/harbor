# 3. Control channel between Harbor.Shell and the dock

- Status: Accepted
- Date: 2026-10-14

## Context

Harbor.Shell supervises the dock: from M1 its watchdog must notice a dead or hung dock and restore the
taskbar. They run as separate processes (C# and C++) in the same user session and need a small,
versioned, safe channel.

## Decision

**Transport and roles**
- Named pipe `\\.\pipe\harbor.control.<sessionId>`, byte mode.
- **Harbor.Shell is the server** (it is the supervisor and will launch the dock); **the dock is the
  client**, reconnecting with backoff (100 ms doubling to 2 s), so it runs fine without the Shell.

**Framing and messages** (see `contracts/README.md`)
- One compact UTF-8 JSON object per line, `{"v":1,"type":"...","payload":{...}}`, at most 64 KiB; longer
  lines are dropped up to the next `\n`.
- `v` is the message's own version. Schemas are strict (`additionalProperties: false`) because all
  executables ship together; any change bumps `v`.
- Decoders reject anything off-schema, including duplicate keys, and the receiver logs and drops it.
- The Shell pings every second; a pong counts only if it answers the latest ping, and its round-trip
  time is reported.
- **The dock answers pings on its UI thread**, so a pong proves the UI thread isn't hung. M1's watchdog
  builds on that.

**Security**
- The pipe's DACL allows only the current user's SID and **denies the NETWORK SID**. System.IO.Pipes
  can't set `PIPE_REJECT_REMOTE_CLIENTS`, and its `CurrentUserOnly` grants the token *owner*, which is
  the whole Administrators group for an elevated admin.
- One instance only: `FILE_FLAG_FIRST_PIPE_INSTANCE` makes the Shell fail instead of joining a squatted
  name (a foreign squatter gives `UnauthorizedAccessException`; a second Shell gives `IOException`,
  `ERROR_PIPE_BUSY`).
- The dock connects with `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION` (the server can't impersonate
  it) and refuses servers whose process runs as another user. A `--pipe-name` override for tests is
  limited to `harbor.[a-z0-9.-]`.

## Alternatives considered

- **Dock as server**: the supervisor (Shell) would then depend on the supervised process being up first.
- **Answering pings on the pipe thread**: simpler, but a hung UI would still answer and the watchdog
  couldn't see it.
- **`PipeOptions.CurrentUserOnly`**: see the owner/Administrators issue above.

## Consequences

- The dock's write path blocks the UI thread for at most 1 s if the Shell stops reading; then it cancels
  I/O and reconnects. Moving writes off the UI thread is an option if that ever matters.
- Integration tests on both sides and an end-to-end test (real `harbor-dock.exe` against the real server)
  cover the protocol, bad input, reconnects and the pipe's ACL.
