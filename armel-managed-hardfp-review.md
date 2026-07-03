# armel Managed Hard-Float ABI — Review Guide

Commit: `942ba67709e [experimental] armel: managed hard-float ABI (DOTNET_JitManagedHardFP)`
Branch: `armel-managed-hardfp` (off `main`)
Status: experimental, on-device verified on armel (Raspberry Pi 4 / Tizen-style Core_Root). OFF by default.

This document explains **what the commit does, why each change exists, and where to scrutinize** during review.

---

## 1. Problem & idea

armel is a **SOFTFP** target: the AAPCS "base" (soft-float) calling convention passes/returns floating-point values in **core integer registers** (r0–r3 / stack), even though the hardware has VFP and all *computation* happens in VFP (s0–s15/d0–d7). Consequently the JIT must emit a **cross-domain `vmov`** at every managed float call boundary:

- caller: compute in VFP → `vmov r,s` to place the arg in a core reg;
- callee: `vmov s,r` to bring the arg back into VFP; and on return `vmov.f2i r0,s0`.

These core↔VFP moves are **cross-domain** (the integer and VFP register files are physically separate) and land on the call's critical path.

**Idea:** let *managed↔managed* calls use the **hard-float (AAPCS-VFP)** convention (floats/HFAs stay in VFP across the call), and apply SOFTFP conversion **only at the native boundary** (P/Invoke, JIT helpers, FCalls, reverse-P/Invoke), which must match the softfp-compiled native runtime/libraries.

**Key invariant of the whole change:** everything is gated on a runtime knob and, where the machinery is compile-time (FEATURE_HFA), each *action* is runtime-gated. With the knob off, behavior is **bit-for-bit the standard SOFTFP path**.

---

## 2. The knob

`DOTNET_JitManagedHardFP=1` enables the experiment. It is read by three components, all from the **same** name:

- **JIT (runtime clrjit):** `JitConfig.JitManagedHardFP()` — declared `CONFIG_INTEGER(JitManagedHardFP,...)` in `jit/jitconfigvalues.h` (guarded `#if defined(TARGET_ARM)`).
- **VM:** `CLRConfig::EXTERNAL_JitManagedHardFP` (`inc/clrconfigvalues.h`), read via `IsArmManagedHardFPEnabled()` in `vm/callingconvention.h` (cached, DAC-safe returns false).
- **crossgen2 (R2R build):** forwarded as a JIT codegen option (`--codegenopt:JitManagedHardFP=1`, or `/p:JitManagedHardFP=true` for the CoreLib build). Read in-process via `JitConfigProvider`.

Because JIT and VM read the same env var, a single `DOTNET_JitManagedHardFP=1` makes JIT-compiled code and the VM's calling-convention engine agree.

---

## 3. Core JIT design (per-call ABI decision)

The base ABI on armel stays SOFTFP (`opts.compUseSoftFP == true`). The experiment overlays a **per-call / per-method** hard-float decision:

- `jit/compiler.h` — two new `opts` fields:
  - `compManagedHardFP` = `compUseSoftFP && JitConfig.JitManagedHardFP()` (feature on).
  - `compSoftFPParams` = whether **this method's own params/return** use SOFTFP. False for a normal managed method under the experiment (hard-float); **true** for a reverse-P/Invoke method (entered from native SOFTFP) or when the knob is off.
  - Set in `jit/compiler.cpp` `compInitOptions`, right after the existing softfp/CONFIGURABLE_ARM_ABI block (guarded `#ifdef TARGET_ARM`).

- **Per-call predicate** — `jit/compiler.hpp` `Compiler::compIsManagedHardFPCall(GenTreeCall*)`:
  ```
  compManagedHardFP && !call->IsUnmanaged() && !call->IsHelperCall()
                    && (call->gtCallMoreFlags & GTF_CALL_M_NOGCCHECK) == 0
  ```
  This is the heart of the boundary rule:
  - `IsUnmanaged()` → explicit P/Invoke / unmanaged calli (and QCalls, which look like P/Invoke): **SOFTFP**.
  - `IsHelperCall()` → JIT helpers (e.g. `CORINFO_HELP_DBLREM`), compiled into the softfp runtime: **SOFTFP**.
  - `GTF_CALL_M_NOGCCHECK` → set at import from `CORINFO_FLG_NOGCCHECK`, which the VM sets **exactly for `MethodDesc::IsFCall()`** (native methods, `-mfloat-abi=softfp`): **SOFTFP**.
  - Everything else = managed → **hard-float**.

- **Classifier** — `jit/abi.h` adds `ClassifierInfo.ForceHardFP`; `jit/targetarm.cpp` `Arm32Classifier::Classify` computes `useSoftFP = comp->opts.compUseSoftFP && !m_info.ForceHardFP`. The existing HFA/float-in-VFP branch runs when `!useSoftFP`.
  - `jit/morph.cpp` (both `AddFinalArgsAndDetermineABIInfo` and `DetermineABIInfo`): `info.ForceHardFP = comp->compIsManagedHardFPCall(call)` — **the outgoing-call injection point**.
  - `jit/lclvars.cpp` `lvaClassifyParameterABI`: `cInfo.ForceHardFP = opts.compUseSoftFP && !opts.compSoftFPParams` — the incoming-params point.

- **Codegen / lowering sites** previously keyed on the global `opts.compUseSoftFP` were re-pointed:
  - Return value of a *call*: `jit/codegenarmarch.cpp` uses a per-call `calleeUsesSoftFP = compUseSoftFP && !compIsManagedHardFPCall(call)` (controls `vmov.f2i`/`vmov.i2d` reassembly vs leaving the result in a float reg).
  - Double arg decomposition to `GT_LONG`: `jit/lower.cpp` `LowerArg` uses a per-call `argUsesSoftFP`.
  - Method's own params/return (prolog homing, return placement, vararg mangling): `jit/lclvars.cpp`, `jit/codegencommon.cpp`, `jit/codegenarm.cpp`, `jit/compiler.hpp` `mangleVarArgsType` — all switched from `compUseSoftFP` to `compSoftFPParams`.

- **Intrinsic → call FCalls** — `jit/rationalize.cpp` `RewriteNodeAsCall`: `[Intrinsic] extern` FCalls (Math.Pow/Ceiling/Sin/…) are imported as `GT_INTRINSIC` and re-materialized as a call in the rationalizer, **bypassing** the import-time NOGCCHECK copy. Fix: after creating the call, copy `GTF_CALL_M_NOGCCHECK` from `getMethodAttribs(callHnd)` (guarded `#ifdef TARGET_ARM`), so `compIsManagedHardFPCall` correctly classifies them SOFTFP. (Floor/Sqrt are hardware-expanded, no call; Pow/Ceiling fall back to the FCall.)

**Review focus:** confirm every former `opts.compUseSoftFP` reader in the ARM JIT is now either (a) per-call via `compIsManagedHardFPCall`, or (b) per-method via `compSoftFPParams`. `git grep compUseSoftFP jit/` should show only the definition, the classifier base value, and the init.

---

## 4. VM (calling-convention engine)

`vm/callingconvention.h`:
- `IsArmManagedHardFPEnabled()` — cached CLRConfig read; `#ifdef DACCESS_COMPILE` returns false.
- `ArgsUseHardFP()` on the ArgIterator bases: **managed** (`ArgIteratorBase`, and the reflection-only `ArgIteratorBaseForMethodInvoke` in `vm/reflectioninvocation.cpp`) → `IsArmManagedHardFPEnabled()`; **P/Invoke** (`ArgIteratorBaseForPInvoke`) → **always false**.
- `GetNextOffset` (ARM): the VFP-placement block is compiled on softfp too; gated at runtime by `fUseVFPRegs = fFloatingPoint && !IsVarArg && ArgsUseHardFP()`. (HFA sets `fFloatingPoint=true`, so it flows through the same gate.)
- `ComputeReturnFlags`: R4/R8 and the HFA return case set the FP return size only when `ArgsUseHardFP()` on armel (arm64 unconditional).

`vm/callhelpers.cpp`: the FEATURE_HFA enregistered-valuetype-return-buffer convention (consumes an extra arg for the return buffer) is gated with `&& IsArmManagedHardFPEnabled()` on armel — otherwise it would change SOFTFP valuetype-return handling. (arm64 unconditional.)

**Why this matters:** the VM ArgIterator is used by reflection Invoke (`CallDescrWorker`), delegate/stub shuffles, and GC ref map computation. Making it hard-float for managed sigs is what fixed reflection returning wrong values. The infrastructure it needs already existed on armel: `CALLDESCR_FPARGREGS` is defined unconditionally (`vm/arm/cgencpu.h`), and `CallDescrWorkerInternal` (`vm/arm/asmhelpers.S`) already loads s0–s15 and handles `fpReturnSize` — **no asm change was needed.**

**Review focus:** the P/Invoke vs managed base-class split is the safety mechanism — verify `ArgIteratorBaseForPInvoke::ArgsUseHardFP()` returns false so native-target arg layout stays SOFTFP.

---

## 5. HFA (structs of 1–4 homogeneous float/double)

ARM32 hard-float (AAPCS-VFP) passes/returns HFAs in VFP registers; SOFTFP does not. To support this:

- `inc/switches.h`: `FEATURE_HFA` now defined for `TARGET_ARM || TARGET_ARM64` (previously excluded armel). This compiles the HFA machinery **in**; every HFA *action* is runtime-gated (below), so SOFTFP mode is unaffected.
- `jit/jit.h` + `jit/compiler.cpp`: `GlobalJitOptions::compFeatureHfa` is made **mutable** for `CONFIGURABLE_ARM_ABI || (TARGET_ARM && ARM_SOFTFP)` (default false), and set `true` when `compManagedHardFP`. On hard-float ARM/ARM64 it remains a compile-time `const true`; the assignment is guarded to the mutable builds (this caused two of the iterative build breaks — the plain x64-hosted `clrjit` for hard-float ARM has `const` compFeatureHfa).
- Arg placement is auto-gated (HFA → `fFloatingPoint=true` → `fUseVFPRegs` requires `ArgsUseHardFP`); HFA return is gated in `ComputeReturnFlags`; the JIT classifier's HFA branch is under `!useSoftFP`.

Verified: `DOTNET_JitDisasm=AddV4` shows `arg0 struct(16) multireg-arg`, args in s0–s3/s4–s7, return in s0–s3 — real ARM32 hard-float HFA.

**Review focus:** because FEATURE_HFA is now compiled for armel, audit that no *other* FEATURE_HFA consumer changes SOFTFP behavior ungated. Investigated ones: interop `classlayoutinfo`/`fieldmarshaler` compute native HFA but the P/Invoke ArgIterator (`ArgsUseHardFP=false`) never uses it; `callhelpers` gated. Known edge (documented, not hit by tests): a reverse-P/Invoke method **returning** an HFA struct would follow the global `compFeatureHfa`; extremely rare.

---

## 6. R2R (ReadyToRun) — genuinely hard-float, ABI-guarded

R2R images bake native code with the ABI fixed at crossgen time. A hard-float JIT caller cannot use a SOFTFP-compiled R2R method (float args/returns in different registers), and vice-versa. Handling:

- **Produce hard-float R2R:** crossgen2's cross-JIT already sets `CORJIT_FLAG_SOFTFP_ABI` for armel, so `compUseSoftFP=true`; passing `--codegenopt:JitManagedHardFP=1` makes `compManagedHardFP=true` → hard-float R2R. `crossgen-corelib.proj` adds it under `/p:JitManagedHardFP=true` for System.Private.CoreLib. (Host x64 JIT is unaffected — `compManagedHardFP` requires `compUseSoftFP`.)
- **Stamp the ABI:** `READYTORUN_FLAG_ARM_MANAGED_HARDFP` (`inc/readytorun.h`, managed mirror in `tools/Common/Internal/Runtime/ReadyToRunConstants.cs`). Set during header emission in `ReadyToRunHeaderNode` (NOT in `GetReadyToRunFlags` — that runs before `JitConfigProvider.Instance` is initialized and asserts).
- **Runtime ABI guard:** `vm/readytoruninfo.cpp`, right after reading the R2R header — on armel, if `(Flags & READYTORUN_FLAG_ARM_MANAGED_HARDFP) != 0` differs from the runtime knob, **skip** the R2R image (return NULL → JIT fallback). This lets a matching image be used and safely rejects a stale/mismatched one instead of silently corrupting. Replaces the earlier blanket "disable R2R under the knob" (`vm/eeconfig.cpp`, now a comment).
- **GC ref map consistency:** the runtime's `ComputeCallRefMap` uses the (now hard-float) C++ ArgIterator, but crossgen's **managed** GC ref map builder (`GCRefMapBuilder.cs`, via `GCRefMapNode`) is a separate implementation. Fix: `GCRefMapBuilder.IsArmelSoftFP(target)` returns false when `JitManagedHardFP` is set, so `TransitionBlock.FromTarget` picks the **armhf** transition block — GC-ref args recorded at hard-float positions, matching the runtime. (hard-float-armel managed convention == armhf; only float/HFA differ from softfp.) Added `JitConfigProvider.InstanceOrNull` (null-safe). This fixes the `CheckGCRefMapEqual` assert ("GC ref map mismatch: System.Number::TryFormatFloat") that only surfaced with hard-float R2R + float formatting.

**Review focus:** the R2R ABI guard (readytoruninfo.cpp) is the safety net; verify the flag test direction. And confirm `TransitionBlock.FromTarget` isn't relied on for armel *type layout* in a way the config-read would perturb (it is only re-pointed inside `GCRefMapBuilder`, and only when the config is available).

---

## 7. Gating invariant (how default SOFTFP is preserved)

The single most important review property: **with `DOTNET_JitManagedHardFP` unset, nothing changes.**

- JIT: `compManagedHardFP=false` → `compIsManagedHardFPCall`=false everywhere, `compSoftFPParams=compUseSoftFP`, `compFeatureHfa=false`. Classifier/codegen behave exactly as before.
- VM: `ArgsUseHardFP()=false` for all bases → ArgIterator/return/callhelpers use SOFTFP; `readytoruninfo` accepts only softfp R2R (flag unset == knob unset).
- crossgen: without the codegenopt, softfp R2R with the flag unset.

On-device confirmation: baseline (knob off) passes the full correctness suite even with a hard-float SPC.dll present (the guard rejects it → JIT softfp fallback). See §9.

---

## 8. Boundary matrix

| Boundary | Direction | Handling |
|---|---|---|
| managed → managed (direct/virtual/interface/delegate) | M→M | hard-float (classifier ForceHardFP + VM ArgIterator) |
| method's own params/return | — | hard-float unless reverse-P/Invoke (`compSoftFPParams`) |
| P/Invoke, unmanaged calli, QCall | M→N | SOFTFP (`IsUnmanaged`) |
| JIT helper (DBLREM/FLTREM/…) | M→N | SOFTFP (`IsHelperCall`) |
| FCall (Math.Pow/Ceiling, etc.) | M→N | SOFTFP (`GTF_CALL_M_NOGCCHECK`, incl. rationalizer path) |
| reverse-P/Invoke (UnmanagedCallersOnly) | N→M | SOFTFP params/return (`compSoftFPParams`/`IsReversePInvoke`) |
| reflection Invoke | — | hard-float (VM ArgIterator + CallDescrWorker) |
| HFA struct by value | M→M | VFP (FEATURE_HFA, gated) |
| R2R method | — | used only if image ABI == runtime mode |

---

## 9. Verification (on-device, rpi4, armel Checked)

Build: `./build.sh --cross --clang --arch armel -rc Checked -hc Checked --subset clr --cmakeargs -DFEATURE_IBCLOGGER=true /p:JitManagedHardFP=true`
Run: `DOTNET_JitManagedHardFP=1 CORE_LIBRARIES=$CR ./corerun HardFPTest.dll` (R2R on).

- **Correctness** (`hardfp-test/HardFPTest.cs`, 10 groups): ALL PASS with the knob alone (R2R on) — direct scalars, virtual/interface, delegates, HFA (V2/V4/D2 + HFA-with-scalars), float helpers, P/Invoke (libm), unmanaged calli + reverse-P/Invoke, reflection Invoke, hot loop, Math.* FCalls. Baseline (knob off) also ALL PASS (no regression). Uses integer-scaled comparisons to avoid float formatting masking value bugs.
- **R2R actually used:** knob on + R2R on → 247 methods JITted vs 1410 with `DOTNET_ReadyToRun=0` (large gap ⇒ hard-float R2R CoreLib loaded, not silently rejected).
- **Microbench** (`hardfp-test/bench/HardFPBench.cs`, best-of-5, 2M non-inlined calls): callee cross-domain `vmov` drops (ScalarF/D 2→1, HfaF 3→0). Wall-clock softfp→hardfp (R2R on): scalar double ~14%, HFA V4 ~30%, HFA D2 ~46% faster; scalar float within rpi4 noise. Interpretation: hard-float replaces expensive cross-domain `vmov.i2d/.d2i/.i2f` with cheap same-domain `vmov s,s` (or none); double/HFA benefit most.

---

## 10. Known gaps / follow-ups (NOT in this commit)

- **`src/native/.../System.Security.Cryptography.Native/opensslshim.c`**: a `sizeof(time_t)==8` static assert fails on the 32-bit-time_t Tizen armel rootfs (unrelated env issue). Worked around locally by commenting it out; **intentionally excluded** from this commit. Needs a rootfs fix or a separate decision.
- **HFA caller code quality:** under hard-float the HFA caller shows extra same-domain `vmov s,s` shuffles (regalloc opportunity). Correct and still net-faster, but improvable.
- **Reverse-P/Invoke returning an HFA struct:** uses the global `compFeatureHfa` rather than a per-method decision (very rare; documented).
- **R2R version guard hardening:** the ABI flag is a functional guard; a more formal R2R version/GUID bump could be considered before any non-experimental use.

---

## 11. Suggested review order

1. `jit/compiler.hpp` `compIsManagedHardFPCall` (the boundary rule) + `jit/compiler.h`/`compiler.cpp` opts.
2. `jit/targetarm.cpp` + `jit/morph.cpp` + `jit/lclvars.cpp` (classification wiring).
3. The codegen/lowering re-points (`codegenarmarch.cpp`, `lower.cpp`, `codegencommon.cpp`, `codegenarm.cpp`, `compiler.hpp mangleVarArgsType`).
4. `jit/rationalize.cpp` (FCall intrinsic path).
5. `vm/callingconvention.h` `ArgsUseHardFP` split + return + `vm/callhelpers.cpp` + `vm/reflectioninvocation.cpp`.
6. `switches.h` FEATURE_HFA + `jit.h`/`compiler.cpp` compFeatureHfa mutability.
7. R2R: `readytoruninfo.cpp` guard, `eeconfig.cpp`, `readytorun.h`/`ReadyToRunConstants.cs` flag, `ReadyToRunHeaderNode.cs`, `GCRefMapBuilder.cs`, `crossgen-corelib.proj`.
8. `hardfp-test/` to see the verification surface.
