# 4. Contract tests and test frameworks

- Status: Accepted
- Date: 2026-10-14

## Context

The dock (C++) and the Shell (C#) each implement the IPC codec. We need proof that both agree with each
other and with the JSON Schemas in `contracts/`, without adding dependencies to shipped code.

## Decision

- **Shared fixtures** in `contracts/fixtures/{valid,invalid}`. Both suites list them at run time, so a
  new fixture is picked up without a rebuild:
  - `tests/dock`: every valid fixture conforms to its message's schema, decodes and re-encodes to equal
    JSON; every invalid fixture matches no schema and is rejected.
  - `tests/Harbor.Core.Tests`: the same fixtures against the C# codec.
  - Schema, C++ and C# therefore agree. Rules JSON Schema can't express (duplicate keys) are unit-tested
    on both sides.
- **Schema validation in C++** with pboettch **json-schema-validator** (MIT, built on nlohmann-json,
  test-only). Schemas use **JSON Schema draft-07**, which covers every keyword we use.
- **Test frameworks**: GoogleTest for C++ (`gtest_discover_tests` with `DISCOVERY_MODE PRE_TEST`, so
  cross-compiled builds never run test binaries at build time); **MSTest.Sdk** on
  Microsoft.Testing.Platform for C#.
- The end-to-end test needs `HARBOR_DOCK_EXE`; locally it is inconclusive without it, in CI it fails.

## Alternatives considered

- **JsonSchema.Net** (C#, best 2020-12 support): rejected because it moved from MIT to the Open Source
  Maintenance Fee EULA.
- **LateApexEarlySpeed.Json.Schema** (BSD-3): smaller project, pulls an old Microsoft.Extensions.Http.
- **NJsonSchema**: pulls Newtonsoft.Json and is aimed at older drafts.
- **No schema validation**: fixtures would still prove C++ and C# agree, but schemas could silently drift.
- **xUnit**: equivalent to MSTest; MSTest is first-party and needs one line (`Sdk="MSTest.Sdk"`).

## Consequences

- Each contract test was shown to fail when a valid message is planted in `invalid/` and when the schema is
  weakened, so they are not vacuous.
- A future move to draft 2020-12 needs a validator that supports it.
