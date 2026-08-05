# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Authoritative build/test reference

`/.github/copilot-instructions.md` is the detailed, per-component source of truth for building and
testing (baseline builds, sentinel artifacts, single-test invocation, troubleshooting). **Read it
before doing any build or test work.** This file summarizes the big picture; that file has the exact
commands. When the two disagree, prefer copilot-instructions.md.

## Repository shape

This is `dotnet/runtime` — the .NET runtime, base class libraries, and the shared host/installers.
It is very large (~1.5 GB checked out, 10–20 GB per built configuration). Everything under `src/`:

- `src/coreclr/` — **CoreCLR**, the primary runtime (C/C++): JIT (`src/coreclr/jit`), GC, VM, NativeAOT, crossgen2, debugging/diagnostics. NativeAOT is part of CoreCLR but has its own build specifics.
- `src/mono/` — **Mono**, the slimmer runtime used for WASM/mobile and some other targets.
- `src/libraries/` — the **base class libraries** (~245 library dirs). Each library follows a `src/` (implementation), `ref/` (reference assembly / public API surface), `tests/` layout, plus a `.csproj`.
- `src/libraries/System.Private.CoreLib/` — the lowest-level managed library, tightly coupled to the runtime; its config must match the runtime's (build with `-rc checked` for asserts).
- `src/native/` — native code shared across components (`corehost/` = the `dotnet` muxer/host, `managed/`, PAL, etc.).
- `src/installer/` — installers and packaging for the host.
- `src/tests/` — the CoreCLR/Mono runtime test suite (separate build system from libraries tests; see below).
- `src/tasks/`, `src/tools/` — MSBuild tasks and tooling used by the build.
- `src/coreclr/System.Private.CoreLib/` — the runtime-specific half of CoreLib (paired with the shared half in `src/libraries/System.Private.CoreLib/src`).

### Three-way component model

Every task maps to one component, which determines the baseline build command. Map by changed path:
`src/coreclr/`→CoreCLR, `src/mono/`→Mono, `src/libraries/`→Libraries (or WASM/WASI Libraries if the
`.csproj` targets browser/wasi), `src/native/corehost` or `src/installer/`→Host,
`src/tests/`→Runtime Tests. See copilot-instructions.md §"Identify Your Component" for the full table.

## Building

The root `build.sh` (Linux/macOS) / `build.cmd` (Windows) drives all builds from the repo root. No
sudo needed. Key ideas:

- **Subsets** select what to build: `clr`, `libs`, `mono`, `host`, `packs`, plus finer-grained ones. Combine with `+`: `./build.sh clr+libs`. The first positional arg can be the subset (the `-subset` flag is optional). Run `./build.sh -subset help` for the full list.
- **Configurations**: `Debug` (asserts, slow, best for debugging), `Checked` (CoreCLR-only: optimized + asserts), `Release` (optimized, no asserts). Set globally with `-c`, or per-subset with `-rc` (runtime), `-lc` (libraries), `-hc` (host).
- Common baseline: `./build.sh clr+libs` (CoreCLR work) or `./build.sh clr+libs -rc release` (libraries work — release runtime, debug libs). A full baseline can take **up to 40 minutes**; do not cancel unless there's been no output for 5+ minutes.
- The build produces a local SDK at `.dotnet/`. Use it via `./dotnet.sh` (or add `.dotnet` to `PATH`); its version must match `sdk.version` in `global.json`.
- **Warnings are errors**, including many style warnings. To disable temporarily while iterating, set env var `TreatWarningsAsErrors=false`.

## Testing

- **Libraries**: `cd src/libraries/<Name>` then `dotnet build` and `dotnet build /t:test ./tests/<TestProject>.csproj`. Running library tests requires a baseline build (`artifacts/bin/testhost/` must exist). Tests use xUnit.
- **CoreCLR / Mono runtime tests** use a separate build system under `src/tests/` (not `dotnet test`): build with `src/tests/build.sh`, generate the `Core_Root` layout, then run with `Core_Root/corerun <Test>.dll` where **exit code 100 = pass**. Use `-priority1` to include priority-1 tests (otherwise the build silently reports "0 test projects"). See copilot-instructions.md for exact invocation.
- Prefer targeted test runs and check the run count / logs to confirm tests actually executed.

## Conventions

- Formatting and naming are enforced by `/.editorconfig` (C#) and `.clang-format`/`.clang-tidy` (native). Follow them exactly.
- Additional C# style expectations (file-scoped namespaces, `is null`/`is not null`, pattern matching/switch expressions, `nameof`, prefer `[Theory]`+`[InlineData]` over duplicative `[Fact]`s, etc.) are listed in copilot-instructions.md.
- Before editing in a directory, read any `README.md` in that directory and its parents up to the repo root — they carry area-specific conventions and build/test guidance.
- Public API changes go through the `ref/` reference assemblies and the API proposal process; see `docs/coding-guidelines/adding-api-guidelines.md`.
- Deeper coding standards live in `docs/coding-guidelines/` (e.g. `coding-style.md`, `clr-code-guide.md`, `clr-jit-coding-conventions.md`, `mono-code-guide.md`, `performance-guidelines.md`, `vectorization-guidelines.md`).

## Task-specific skills

This repo ships Copilot/Claude skills under `.github/skills/` for specialized work — invoke the
matching one instead of improvising. Notable ones:

- `code-review` — required review flow for PRs (see `.github/code-review-instructions.md`).
- `vectorization` — required when writing/reviewing SIMD or `System.Runtime.Intrinsics.*` code.
- `performance-benchmark` — validate perf-affecting changes before completing.
- `api-proposal`, `breaking-change-doc`, `add-new-jit-ee-api`, `jit-regression-test`, `issue-triage`, `mobile-platforms`, and CI-related skills (`ci-pipeline-monitor`, `pr-failure-scan`).

## Contributing / CI

- Follow `CONTRIBUTING.md` and `docs/workflow/ci/pr-guide.md` before submitting a PR.
- CI triage guidance: `docs/workflow/ci/failure-analysis.md` and `triaging-failures.md`. Given the repo's size, some test flakiness is expected.
- Committed code MUST compile and related tests MUST pass — verify by actually building/running, don't assume.
