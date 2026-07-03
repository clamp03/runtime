# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

This is a clone of **dotnet/runtime** — the source for the .NET runtimes (CoreCLR and Mono), the base libraries, and the host/installers. It is a very large repo; a single-config build consumes 10–20 GB of disk. Do not attempt to build the whole thing — always scope work to a subset (see below).

The local workflow here is specialized: cross-compiling **CoreCLR for `armel` (ARM32) targeting Tizen** and deploying to a device over `sdb` (Samsung Debug Bridge). Most active work is in the **JIT** and **CoreCLR VM**.

## Build system essentials

The root `build.sh` (→ `eng/build.sh`) is the entry point. It always juggles two platforms: the **build platform** (this machine, Linux x64) and the **target platform** (often `armel`). Key concepts:

- **Subsets** (`-subset` / first positional arg, `+`-joined): `clr` (runtime + CoreLib), `libs`, `host`, `packs`, `mono`. Run `./build.sh -subset help` for the full list. Never omit `-subset` for real work — a bare `./build.sh` builds everything.
- **Configurations**: `Debug` (no opt, asserts on), `Release` (opt, asserts off), and CoreCLR-only `Checked` (opt + asserts — this is the config used for running tests/CI and for the local armel workflow).
- **Per-subset config flags**: `-runtimeConfiguration`/`-rc`, `-librariesConfiguration`/`-lc`, `-hostConfiguration`/`-hc`. The general `-c` applies to any subset not qualified by a specific flag. **Gotcha:** building `clr` also builds `apphost` (a `host` component); if you only pass `-rc`/`-lc`, `apphost` defaults to Debug and breaks test builds. Always also pass `-c` or `-hc` when building `clr`.
- Build outputs: `artifacts/bin/coreclr/<OS>.<arch>.<config>/` (key binaries: `corerun`, `libcoreclr.so`, `System.Private.CoreLib.dll`), logs in `artifacts/log/`, intermediates in `artifacts/obj/`.
- **Warnings are errors**, including many style warnings. To iterate without them, `export TreatWarningsAsErrors=false` before building.
- Use `./dotnet.sh <args>` (not a system `dotnet`) — it bootstraps the repo-local SDK pinned in `global.json`.

### The local armel / Tizen workflow (custom scripts at repo root)

These untracked scripts encode the primary workflow here. `ROOTFS_DIR` must point at a pre-built armel crossrootfs.

- `build.arm.chk.sh` — cross-builds `clr+libs`, Checked runtime + Release libraries, for `armel` via clang:
  ```bash
  export ROOTFS_DIR=/home/clamp/Work.bak/dotnet/rootfs/armel
  ./build.sh --cross --clang --arch armel -rc Checked -lc Release -hc Checked \
    --subset clr+libs --cmakeargs -DFEATURE_IBCLOGGER=true --bootstrap
  ```
- `build.arm.chk.test.sh` — builds priority-1 CoreCLR tests for armel (cross):
  ```bash
  ./src/tests/build.sh -armel -Checked -priority1 -cross \
    -p:UseLocalAppHostPack=true -p:LibrariesConfiguration=Release -p:SelfContained=false -p:PublishSingleFile=false
  ```
- `auto.arm.chk.sh` — runs `build.arm.chk.sh`, then tars the coreclr and runtime artifacts and `sdb push`es them to the device's `Core_Root`. This is the deploy step to the Tizen device.

Cross-compiling to armel/Tizen also relies on the prereqs Docker image `ubuntu-22.04-cross-armel-tizen`; see `docs/workflow/using-docker.md` and `docs/workflow/building/coreclr/cross-building.md`.

## Testing CoreCLR

The reliable way to run CoreCLR tests is via **Core_Root** — a runnable bundle of the runtime + library packages. Requires `libs` built first.

- Generate Core_Root (Checked clr, x64 shown):
  ```bash
  ./src/tests/build.sh -arch x64 -checked -generatelayoutonly
  ```
  Output: `artifacts/tests/coreclr/<OS>.<arch>.<config>/Tests/Core_Root`.
- Build a single test (repeatable `-test`), or a directory of tests (`-dir`):
  ```bash
  ./src/tests/build.sh <arch> <config> -test src/tests/JIT/.../Test.csproj
  ```
- Run a single test: set `CORE_ROOT` (or pass `-coreroot`) and run the generated `<Test>.sh` wrapper. Results land in `artifacts/tests/coreclr/<OS>.<arch>.<config>/Reports/...`.
- Filter within a merged/standalone test runner at build time: `./dotnet.sh build -c Checked <test>.csproj -p:TestFilter=<SubstringOfFQN>`.

Full details: `docs/workflow/testing/coreclr/testing.md`. Libraries tests are xunit-based and documented separately under `docs/workflow/testing/libraries/`.

## Code architecture

Three top-level components under `src/`:

- **`src/coreclr/`** — the primary CoreCLR runtime (C/C++). Notable subtrees:
  - `jit/` — the RyuJIT compiler. Architecture-specific codegen lives in files like `codegenarm64.cpp`, `codegenxarch.cpp`, etc. Heavy local focus.
  - `vm/` — the VM: type system, GC integration, threading, interop, appdomain, JIT interface (`ICorJitInfo`). Per-arch asm helpers under `vm/amd64`, `vm/arm`, etc.
  - `gc/`, `pal/` (platform abstraction for non-Windows), `nativeaot/`, `interpreter/`, `debug/`, `md/` (metadata), `tools/` (crossgen2, R2RDump, etc.).
  - `System.Private.CoreLib/` — CoreCLR-specific managed CoreLib; runtime-agnostic parts are in `src/libraries/System.Private.CoreLib/src`. **CoreLib config must match the runtime config.**
- **`src/mono/`** — the Mono runtime (used for WASM, mobile, and other constrained targets).
- **`src/libraries/`** — the managed BCL. Each library is its own project with `src/`, `ref/` (reference assemblies), and `tests/`. Managed library code is architecture-independent.

Other: `src/tests/` (CoreCLR test suite + `build.sh`/`run.sh`), `src/installer/`, `src/native/`, `src/tools/`.

## Coding conventions

Follow the existing style; the enforced rules live in `.editorconfig` / `.clang-format` and are treated as build errors. When touching runtime code, consult the relevant guide in `docs/coding-guidelines/`:
- `coding-style.md` (C#), `clr-code-guide.md` and `clr-jit-coding-conventions.md` (CoreCLR/JIT C++), `mono-code-guide.md`.
- `vectorization-guidelines.md`, `performance-guidelines.md`, `interop-guidelines.md` for those domains.
- API changes: `adding-api-guidelines.md` and `framework-design-guidelines-digest.md` (ref-assembly updates via `updating-ref-source.md`).

## Docs map

`docs/workflow/` is the authoritative guide: `README.md` (build/config overview), `building/{coreclr,libraries,mono}/`, `testing/`, `debugging/`, `requirements/`. `docs/project/glossary.md` decodes the many acronyms. `docs/area-owners.md` maps subsystems to owners.
