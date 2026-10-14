# 1. JSON library for the C++ dock: nlohmann-json

- Status: Accepted
- Date: 2026-10-14

## Context

The dock must parse and produce the newline-delimited JSON IPC messages (`contracts/`). Traffic is tiny:
about one control message per second and, from M4, one stats snapshot per second. The dock has a strict
footprint budget (< 40 MB working set, < 0.5 % idle CPU), and bad input must never crash it.

## Decision

Use **nlohmann-json** (vcpkg `nlohmann-json`, 3.12.0, MIT, header-only).

- Parse with `allow_exceptions = false` and type-check every access, so decoding never throws.
- A parser callback rejects duplicate keys (nlohmann otherwise keeps the last one, which would disagree
  with System.Text.Json on the Shell side).
- Encode with `ordered_json` so messages keep the documented field order in logs.

## Alternatives considered

- **simdjson**: much faster, but parse-only; we would need a second library to write JSON. Speed is
  irrelevant at our message rate.
- **Hand-written parser**: no dependency, but parsing untrusted input by hand is a bug farm.
- **Boost.JSON / glaze**: heavier dependency, or C++23-only, for no benefit here.

## Consequences

- One header-only dependency, which also lets the contract tests use json-schema-validator (built on it).
- Measured at M0: dock Release, idle with 1 Hz pings: 0.006 % CPU, 13.5 MB working set (ARM64).
