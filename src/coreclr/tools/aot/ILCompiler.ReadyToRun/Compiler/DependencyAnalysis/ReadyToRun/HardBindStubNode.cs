// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics;

using Internal.Text;
using Internal.TypeSystem;

namespace ILCompiler.DependencyAnalysis.ReadyToRun
{
    /// <summary>
    /// Hard-bind fallback stub: a tiny per-callee stub that routes a would-be direct call
    /// through the callee's regular method import cell. It is the relaxation target for
    /// direct-call relocations whose callee body cannot be safely hard-bound (the callee
    /// did not compile into this image, or was compiled in a late phase, or direct binding
    /// would form a preparation cycle). Routing through the cell preserves the full lazy
    /// delay-load protocol: the first traversal goes through the cell's delay-load thunk
    /// (ExternalMethodFixupWorker: EnsureActive + prestub + per-method fixups), subsequent
    /// traversals jump to the resolved target.
    ///
    /// Per-architecture shapes match the delay-load thunk's expectations for identifying
    /// the indirection cell. The stub is jump-shaped (it must not push a return address:
    /// a new return address inside the stub would not resolve to any managed method during
    /// GC stack walks). On x64 that means the cell must be a jumpable import (thunk kind
    /// DelayLoadHelperWithExistingIndirectionCell, rax = cell), exactly like R2R fast
    /// tailcalls (see updateEntryPointForTailCall):
    ///   x64:   lea rax, [rip+cell]; jmp [rax]        (rax = indirection cell)
    ///   arm:   movw/movt r12, #cell; ldr pc, [r12]   (r12 = REG_R2R_INDIRECT_PARAM)
    ///   arm64: mov x11, #cell; ldr x16, [x11]; br x16 (x11 = REG_R2R_INDIRECT_PARAM)
    ///
    /// The stub registers itself in the DelayLoadMethodCallThunks range so the runtime
    /// classifies its PCs as STUB_CODE_BLOCK_METHOD_CALL_THUNK, exactly like the import
    /// thunks whose calling convention it shares. Its ClassCode is chosen to sort
    /// immediately after ImportThunk (433266948) keeping that range contiguous.
    /// </summary>
    public class HardBindStubNode : AssemblyStubNode, ISortableSymbolNode
    {
        private readonly Import _cell;

        public HardBindStubNode(Import cell)
        {
            Debug.Assert(cell.RepresentsIndirectionCell);
            _cell = cell;
        }

        public Import Cell => _cell;

        /// <summary>
        /// Set by HardBindRelaxation when at least one direct-call relocation was
        /// redirected to this stub. Unused stubs (the common case: their callee's direct
        /// calls were all kept) are skipped at emission.
        /// </summary>
        public bool Used { get; private set; }

        public void MarkUsed(NodeFactory factory)
        {
            if (!Used)
            {
                Used = true;
                // Register with the method-call-thunk range only when actually emitted so
                // the runtime classifies the stub's PCs as STUB_CODE_BLOCK_METHOD_CALL_THUNK.
                factory.DelayLoadMethodCallThunks.OnNodeInRangeMarked(this);
            }
        }

        public override bool ShouldSkipEmittingObjectNode(NodeFactory factory) => !Used;

        public override void AppendMangledName(NameMangler nameMangler, Utf8StringBuilder sb)
        {
            sb.Append("HardBindStub->"u8);
            _cell.AppendMangledName(nameMangler, sb);
        }

        protected override string GetName(NodeFactory factory)
        {
            Utf8StringBuilder sb = new Utf8StringBuilder();
            AppendMangledName(factory.NameMangler, sb);
            return sb.ToString();
        }

        // Sorts immediately after ImportThunk (433266948) so that the
        // DelayLoadMethodCallThunks [start, end] range stays contiguous.
        public override int ClassCode => 433266949;

        public override int CompareToImpl(ISortableNode other, CompilerComparer comparer)
        {
            return comparer.Compare(_cell, ((HardBindStubNode)other)._cell);
        }

        protected override DependencyList ComputeNonRelocationBasedDependencies(NodeFactory factory)
        {
            Debug.Assert(base.ComputeNonRelocationBasedDependencies(factory) == null);
            DependencyList dependencies = new DependencyList();
            dependencies.Add(factory.DelayLoadMethodCallThunks, "MethodCallThunksList");
            return dependencies;
        }

        protected override void EmitCode(NodeFactory factory, ref X64.X64Emitter instructionEncoder, bool relocsOnly)
        {
            // The cell must be a jumpable import: its delay-load thunk
            // (DelayLoadHelperWithExistingIndirectionCell) expects the indirection cell
            // address in rax instead of decoding it from the return address, which a
            // jump-shaped callsite does not have.
            //
            // lea rax, [rip+cell]
            instructionEncoder.EmitLEAQ(X64.Register.RAX, _cell);
            // jmp [rax]
            X64.AddrMode jmpAddrMode = new X64.AddrMode(X64.Register.RAX, null, 0, 0, X64.AddrModeSize.Int64);
            instructionEncoder.EmitJmpToAddrMode(ref jmpAddrMode);
        }

        protected override void EmitCode(NodeFactory factory, ref X86.X86Emitter instructionEncoder, bool relocsOnly)
        {
            throw new NotImplementedException();
        }

        protected override void EmitCode(NodeFactory factory, ref ARM.ARMEmitter instructionEncoder, bool relocsOnly)
        {
            // movw/movt r12, #cell (r12 = REG_R2R_INDIRECT_PARAM: the delay-load thunk
            // expects the indirection cell address in r12)
            instructionEncoder.EmitMOV(ARM.Register.R12, _cell);
            // ldr pc, [r12] -- branch through the cell, leaving r12 = cell address
            // (r15 = pc; loading pc from memory is an architected branch in Thumb2)
            instructionEncoder.EmitLDR(ARM.Register.R15, ARM.Register.R12);
        }

        protected override void EmitCode(NodeFactory factory, ref ARM64.ARM64Emitter instructionEncoder, bool relocsOnly)
        {
            // x11 = REG_R2R_INDIRECT_PARAM: the delay-load thunk expects the indirection
            // cell address in x11
            instructionEncoder.EmitMOV(ARM64.Register.X11, _cell);
            // ldr x16, [x11]; br x16
            instructionEncoder.EmitLDR(ARM64.Register.X16, ARM64.Register.X11);
            instructionEncoder.EmitJMP(ARM64.Register.X16);
        }

        protected override void EmitCode(NodeFactory factory, ref LoongArch64.LoongArch64Emitter instructionEncoder, bool relocsOnly)
        {
            throw new NotImplementedException();
        }

        protected override void EmitCode(NodeFactory factory, ref RiscV64.RiscV64Emitter instructionEncoder, bool relocsOnly)
        {
            throw new NotImplementedException();
        }

        protected override void EmitCode(NodeFactory factory, ref Wasm.WasmEmitter instructionEncoder, bool relocsOnly)
        {
            throw new NotImplementedException();
        }
    }
}
