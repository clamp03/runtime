// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;

using Internal.Text;
using Internal.ReadyToRunConstants;
using Internal.JitInterface;

namespace ILCompiler.DependencyAnalysis.ReadyToRun
{
    /// <summary>
    /// READYTORUN_FIXUP_Check_VirtualSlot blob: emitted into a caller's per-method fixup list
    /// for every --hard-bind fragile vtable dispatch call site. The runtime verifies that the
    /// method got the vtable slot the compiler predicted and that the baked MethodTable layout
    /// offsets match the runtime's; on mismatch the caller's precompiled code is rejected
    /// (JIT fallback), so a wrong prediction only ever costs performance.
    ///
    /// Wire format (after the fixup kind byte):
    ///   UInt      flags (reserved, 0)
    ///   MethodSig method the call site dispatches on (its runtime MethodDesc::GetSlot() is
    ///             compared against expectedSlot)
    ///   UInt      expectedSlot
    ///   UInt      expectedOffsetOfIndirection
    ///   UInt      expectedOffsetAfterIndirection
    /// </summary>
    public class CheckVirtualSlotSignature : Signature, IEquatable<CheckVirtualSlotSignature>
    {
        private readonly MethodWithToken _method;
        private readonly uint _slot;
        private readonly uint _offsetOfIndirection;
        private readonly uint _offsetAfterIndirection;

        public CheckVirtualSlotSignature(MethodWithToken method, uint slot, uint offsetOfIndirection, uint offsetAfterIndirection)
        {
            _method = method;
            _slot = slot;
            _offsetOfIndirection = offsetOfIndirection;
            _offsetAfterIndirection = offsetAfterIndirection;

            // Ensure types in signature are loadable and resolvable, otherwise we'll fail later while emitting the signature
            ((CompilerTypeSystemContext)method.Method.Context).EnsureLoadableMethod(method.Method);
        }

        public override int ClassCode => 668091107;

        public override ObjectData GetData(NodeFactory factory, bool relocsOnly = false)
        {
            ObjectDataSignatureBuilder dataBuilder = new ObjectDataSignatureBuilder(factory, relocsOnly);

            if (!relocsOnly)
            {
                dataBuilder.AddSymbol(this);

                SignatureContext innerContext = dataBuilder.EmitFixup(factory, ReadyToRunFixupKind.Check_VirtualSlot, _method.Token.Module, factory.SignatureContext);
                dataBuilder.EmitUInt(0); // flags (reserved)
                dataBuilder.EmitMethodSignature(_method, enforceDefEncoding: false, enforceOwningType: false, innerContext, isInstantiatingStub: false);
                dataBuilder.EmitUInt(_slot);
                dataBuilder.EmitUInt(_offsetOfIndirection);
                dataBuilder.EmitUInt(_offsetAfterIndirection);
            }

            return dataBuilder.ToObjectData();
        }

        public override void AppendMangledName(NameMangler nameMangler, Utf8StringBuilder sb)
        {
            sb.Append(nameMangler.CompilationUnitPrefix);
            sb.Append($@"CheckVirtualSlotSignature(slot {_slot} @ {_offsetOfIndirection}/{_offsetAfterIndirection}): ");
            _method.AppendMangledName(nameMangler, sb);
        }

        public override int CompareToImpl(ISortableNode other, CompilerComparer comparer)
        {
            CheckVirtualSlotSignature otherNode = (CheckVirtualSlotSignature)other;
            int result = _slot.CompareTo(otherNode._slot);
            if (result != 0)
                return result;

            result = _offsetOfIndirection.CompareTo(otherNode._offsetOfIndirection);
            if (result != 0)
                return result;

            result = _offsetAfterIndirection.CompareTo(otherNode._offsetAfterIndirection);
            if (result != 0)
                return result;

            return _method.CompareTo(otherNode._method, comparer);
        }

        public override string ToString()
        {
            return $"CheckVirtualSlotSignature {_method} slot={_slot} offsets={_offsetOfIndirection}/{_offsetAfterIndirection}";
        }

        public bool Equals(CheckVirtualSlotSignature other) => object.ReferenceEquals(other, this);
    }
}
