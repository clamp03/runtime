// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections.Concurrent;
using System.Collections.Generic;

using Internal.TypeSystem;
using Internal.TypeSystem.Ecma;

namespace ILCompiler
{
    /// <summary>
    /// Compile-time approximation of the vtable slot numbers the CoreCLR runtime assigns in
    /// MethodTableBuilder::PlaceVirtualMethods (vm/methodtablebuilder.cpp), used by the
    /// --hard-bind fragile vtable dispatch optimization: eligible in-bubble virtual calls are
    /// emitted as direct vtable dispatch (two dependent loads + indirect call) instead of a
    /// VirtualStubDispatch import cell.
    ///
    /// Every consumer of a slot computed here must also emit a READYTORUN_FIXUP_Check_VirtualSlot
    /// fixup into the calling method's fixup list; the runtime rejects the caller's precompiled
    /// code (JIT fallback) if the actual slot assignment or MethodTable layout diverges. A wrong
    /// or bailed (-1) answer therefore only ever costs performance, never correctness.
    ///
    /// Stage 1 mirrors only the trivially predictable subset of the runtime algorithm:
    /// non-generic Ecma types with no MethodImpls, no runtime-async (Task-returning) virtuals
    /// and no ambiguous name/signature matches anywhere in the base hierarchy. Everything else
    /// bails to the regular VSD path.
    /// </summary>
    public sealed class CoreClrVirtualSlotAlgorithm
    {
        // MethodTable layout constants mirrored from the runtime:
        //
        //   SIZEOF__MethodTable_ = 0x10 + (6 INDEBUG(+1)) * TARGET_POINTER_SIZE
        //                                                     (vm/methodtable.h, GetVtableOffset)
        //   The 0x10 bytes are the non-pointer prefix (m_dwFlags, m_BaseSize, m_dwFlags2,
        //   m_wNumVirtuals + m_wNumInterfaces) followed by 6 pointer-sized fields
        //   (m_pParentMethodTable, m_pModule, m_pAuxiliaryData, m_pEEClass/m_pCanonMT union,
        //   m_pPerInstInfo/m_ElementTypeHnd union, m_pInterfaceMap union) - plus one extra
        //   debug-only pointer on Debug/Checked runtime builds (the INDEBUG(+1)).
        //
        //   VTABLE_SLOTS_PER_CHUNK = 8 (LOG2 = 3)             (vm/methodtable.h)
        //   GetIndexOfVtableIndirection(slot) = slot >> 3     (vm/methodtable.inl)
        //   GetIndexAfterVtableIndirection(slot) = slot & 7   (vm/methodtable.inl)
        //   Indirection chunk pointers are placed immediately after the MethodTable header and
        //   slot entries are plain code pointers (VTableIndir2_t = PCODE).
        public const int VtableSlotsPerChunkLog2 = 3;
        public const int VtableSlotsPerChunk = 1 << VtableSlotsPerChunkLog2;

        /// <summary>
        /// MethodTable::GetVtableOffset() for the target runtime flavor.
        /// <paramref name="debugRuntimeLayout"/> selects the Debug/Checked runtime MethodTable
        /// header layout (one extra debug-only pointer); product images target Release (false).
        /// </summary>
        public static uint GetVtableOffset(int pointerSize, bool debugRuntimeLayout)
        {
            return (uint)(0x10 + (debugRuntimeLayout ? 7 : 6) * pointerSize);
        }

        /// <summary>
        /// Offset from the MethodTable pointer to the vtable indirection chunk pointer holding
        /// the given slot (CEEInfo::getMethodVTableOffset's pOffsetOfIndirection).
        /// </summary>
        public static uint GetOffsetOfIndirection(int slot, int pointerSize, bool debugRuntimeLayout)
        {
            return GetVtableOffset(pointerSize, debugRuntimeLayout) + (uint)((slot >> VtableSlotsPerChunkLog2) * pointerSize);
        }

        /// <summary>
        /// Offset from the indirection chunk pointer to the slot entry
        /// (CEEInfo::getMethodVTableOffset's pOffsetAfterIndirection).
        /// </summary>
        public static uint GetOffsetAfterIndirection(int slot, int pointerSize)
        {
            return (uint)((slot & (VtableSlotsPerChunk - 1)) * pointerSize);
        }

        private sealed class TypeVirtualLayout
        {
            /// <summary>Per virtual slot, the most derived declaration occupying it (what the
            /// runtime's parent method hash matches against, CreateMethodChainHash).</summary>
            public List<MethodDesc> SlotDecls;

            /// <summary>Slot assigned to each virtual instance method declared on this type.</summary>
            public Dictionary<MethodDesc, int> MethodSlots;
        }

        /// <summary>Sentinel meaning "slot assignment for this hierarchy cannot be predicted".</summary>
        private static readonly TypeVirtualLayout s_bailed = new TypeVirtualLayout();

        private readonly ConcurrentDictionary<TypeDesc, TypeVirtualLayout> _layouts = new ConcurrentDictionary<TypeDesc, TypeVirtualLayout>();

        /// <summary>
        /// Predicted runtime vtable slot (MethodDesc::GetSlot()) of a virtual instance method
        /// declared on <paramref name="method"/>.OwningType, or -1 when prediction is not
        /// possible and the caller must fall back to VSD dispatch.
        /// </summary>
        public int GetVirtualSlot(MethodDesc method)
        {
            TypeVirtualLayout layout = GetLayout(method.OwningType);
            if (layout == s_bailed || !layout.MethodSlots.TryGetValue(method, out int slot))
                return -1;
            return slot;
        }

        private TypeVirtualLayout GetLayout(TypeDesc type)
        {
            return _layouts.GetOrAdd(type, ComputeLayout);
        }

        private TypeVirtualLayout ComputeLayout(TypeDesc type)
        {
            // Stage-1 bails: only plain Ecma metadata types (no instantiated generics anywhere
            // in the hierarchy, hence no substitution during signature matching), no interfaces
            // (their slot numbering is unrelated to vtable dispatch), no canonical forms.
            if (type is not EcmaType ecmaType
                || type.IsInterface
                || type.HasInstantiation
                || type.IsCanonicalSubtype(CanonicalFormKind.Any))
            {
                return s_bailed;
            }

            TypeVirtualLayout parentLayout = null;
            DefType baseType = type.BaseType;
            if (baseType != null)
            {
                parentLayout = GetLayout(baseType);
                if (parentLayout == s_bailed)
                    return s_bailed;
            }

            // MethodImpls (explicit overrides, covariant returns, explicit interface
            // implementations) can change which MethodDesc occupies a slot and thereby what
            // derived types' name/signature matching finds. Bail on any type that declares them.
            if (ecmaType.MetadataReader.GetTypeDefinition(ecmaType.Handle).GetMethodImplementations().Count > 0)
                return s_bailed;

            var layout = new TypeVirtualLayout
            {
                SlotDecls = parentLayout != null ? new List<MethodDesc>(parentLayout.SlotDecls) : new List<MethodDesc>(),
                MethodSlots = new Dictionary<MethodDesc, int>()
            };

            int numParentVirtuals = layout.SlotDecls.Count;
            List<MethodDesc> declaredVirtuals = null;

            // MethodTableBuilder::PlaceVirtualMethods: iterate declared methods in MethodDef
            // metadata order (EcmaType.GetMethods yields handles in that order), considering
            // only methods with the metadata Virtual flag that are not Static.
            foreach (EcmaMethod method in ecmaType.GetMethods())
            {
                if (!method.IsVirtual || method.Signature.IsStatic)
                    continue;

                // Runtime-async: the runtime adds a second "async variant" declared method for
                // every Task/ValueTask-returning method, consuming an extra vtable slot right
                // after the Task-returning one (methodtablebuilder.cpp declared-method setup +
                // ClassifyMethodReturnKind). This model does not replicate those slots; bail.
                if (method.IsAsync || IsTaskReturning(method.Signature))
                    return s_bailed;

                // Two same-name same-signature virtuals within one type make the runtime's
                // parent matching order-dependent (and they fight over one slot); bail.
                if (declaredVirtuals != null)
                {
                    foreach (MethodDesc previous in declaredVirtuals)
                    {
                        if (previous.Name == method.Name && previous.Signature.EquivalentTo(method.Signature))
                            return s_bailed;
                    }
                }
                (declaredVirtuals ??= new List<MethodDesc>()).Add(method);

                // MethodTableBuilder::LoaderFindMethodInParentClass: a non-newslot virtual
                // reuses the slot of the parent virtual matching by name + signature; otherwise
                // (newslot, or no match) it gets the next fresh slot. More than one parent match
                // makes the runtime's answer depend on hash-chain order; bail.
                int slot = -1;
                if (!method.IsNewSlot && numParentVirtuals > 0)
                {
                    for (int i = 0; i < numParentVirtuals; i++)
                    {
                        MethodDesc parentDecl = layout.SlotDecls[i];
                        if (parentDecl.Name == method.Name && parentDecl.Signature.EquivalentTo(method.Signature))
                        {
                            if (slot != -1)
                                return s_bailed;
                            slot = i;
                        }
                    }
                }

                if (slot == -1)
                {
                    slot = layout.SlotDecls.Count;
                    layout.SlotDecls.Add(method);
                }
                else
                {
                    // Override: this method becomes the slot's most derived declaration
                    // (PlaceVirtualMethods places it "in the inherited slot as both the Decl
                    // and the Impl").
                    layout.SlotDecls[slot] = method;
                }

                layout.MethodSlots.Add(method, slot);
            }

            return layout;
        }

        /// <summary>
        /// Matches the runtime's ClassifyMethodReturnKind: methods returning
        /// System.Threading.Tasks.Task/Task`1/ValueTask/ValueTask`1 get a runtime-async variant.
        /// Deliberately does not check that the type comes from the system module - over-bailing
        /// is safe.
        /// </summary>
        private static bool IsTaskReturning(MethodSignature signature)
        {
            if (signature.ReturnType.GetTypeDefinition() is MetadataType returnTypeDef
                && returnTypeDef.Namespace == "System.Threading.Tasks"u8
                && (returnTypeDef.Name == "Task"u8
                    || returnTypeDef.Name == "Task`1"u8
                    || returnTypeDef.Name == "ValueTask"u8
                    || returnTypeDef.Name == "ValueTask`1"u8))
            {
                return true;
            }

            return false;
        }
    }
}
