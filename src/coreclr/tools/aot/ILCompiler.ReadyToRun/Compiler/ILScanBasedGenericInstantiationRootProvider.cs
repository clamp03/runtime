// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using Internal.IL;
using Internal.JitInterface;
using Internal.TypeSystem;
using Internal.TypeSystem.Ecma;

namespace ILCompiler
{
    /// <summary>
    /// Scans IL bodies of all methods in a module to discover concrete generic
    /// instantiations (e.g. GenericStorage&lt;LibTest&gt;) and roots the referenced
    /// instantiated methods for AOT compilation.
    ///
    /// This complements the canonical (__Canon) compilation done by
    /// <see cref="ReadyToRunLibraryRootProvider"/> by adding specific instantiations
    /// that are actually used in the code — especially important for value-type
    /// type arguments where shared generics do not apply.
    /// </summary>
    public class ILScanBasedGenericInstantiationRootProvider : ICompilationRootProvider
    {
        private readonly EcmaModule _module;
        private readonly InstructionSetSupport _instructionSetSupport;

        public ILScanBasedGenericInstantiationRootProvider(EcmaModule module)
        {
            _module = module;
            _instructionSetSupport = ((ReadyToRunCompilerContext)module.Context).InstructionSetSupport;
        }

        public void AddCompilationRoots(IRootingServiceProvider rootProvider)
        {
            HashSet<MethodDesc> discoveredMethods = new HashSet<MethodDesc>();

            // Phase 1: Scan IL of every method in the module to find concrete
            //          generic method references.
            foreach (MetadataType type in _module.GetAllTypes())
            {
                foreach (MethodDesc method in type.GetAllMethods())
                {
                    if (method.IsAbstract || method.IsInternalCall)
                        continue;

                    try
                    {
                        ScanMethodIL(method, discoveredMethods);
                    }
                    catch (TypeSystemException)
                    {
                        // Skip methods whose types/signatures cannot be loaded.
                        continue;
                    }
                }
            }

            // Phase 2: Root every discovered concrete generic method.
            foreach (MethodDesc method in discoveredMethods)
            {
                try
                {
                    if (!CorInfoImpl.ShouldSkipCompilation(_instructionSetSupport, method))
                    {
                        ReadyToRunLibraryRootProvider.CheckCanGenerateMethod(method);
                        rootProvider.AddCompilationRoot(method, rootMinimalDependencies: false,
                            reason: "IL scan based generic instantiation");
                    }
                }
                catch (TypeSystemException)
                {
                    continue;
                }
            }
        }

        private void ScanMethodIL(MethodDesc method, HashSet<MethodDesc> discoveredMethods)
        {
            // Obtain the typical (uninstantiated) method definition so we can
            // read the raw IL from metadata.
            MethodDesc typicalMethod = method.GetTypicalMethodDefinition();
            if (typicalMethod is not EcmaMethod ecmaMethod)
                return;

            EcmaMethodIL methodIL = EcmaMethodIL.Create(ecmaMethod);
            if (methodIL == null)
                return;

            byte[] ilBytes = methodIL.GetILBytes();
            if (ilBytes == null)
                return;

            ILReader reader = new ILReader(ilBytes);

            while (reader.HasNext)
            {
                ILOpcode opcode = reader.ReadILOpcode();

                switch (opcode)
                {
                    // Opcodes that carry an inline method token
                    case ILOpcode.jmp:
                    case ILOpcode.call:
                    case ILOpcode.callvirt:
                    case ILOpcode.newobj:
                    case ILOpcode.ldftn:
                    case ILOpcode.ldvirtftn:
                    {
                        int token = reader.ReadILToken();
                        try
                        {
                            object obj = methodIL.GetObject(token, NotFoundBehavior.ReturnNull);
                            if (obj is MethodDesc calledMethod)
                            {
                                TryAddConcreteGenericMethod(calledMethod, discoveredMethods);
                            }
                        }
                        catch (TypeSystemException)
                        {
                            // Token resolution can fail for external types – skip.
                        }
                        break;
                    }

                    default:
                        reader.Skip(opcode);
                        break;
                }
            }
        }

        /// <summary>
        /// If <paramref name="method"/> belongs to a concrete generic instantiation
        /// (not a definition, not canonical, no signature variables), add it to the
        /// set so it will be rooted for compilation.
        /// Also handles generic method instantiations (e.g. Foo&lt;int&gt;()).
        /// </summary>
        private static void TryAddConcreteGenericMethod(MethodDesc method, HashSet<MethodDesc> discoveredMethods)
        {
            // -- Check owning type instantiation --
            TypeDesc owningType = method.OwningType;
            bool owningTypeIsConcreteGeneric = false;

            if (owningType.HasInstantiation && !owningType.IsGenericDefinition)
            {
                if (owningType.ContainsSignatureVariables())
                    return;

                // At least one type argument must be non-Canon for this to be interesting.
                bool allCanon = true;
                foreach (TypeDesc arg in owningType.Instantiation)
                {
                    if (!arg.IsCanonicalDefinitionType(CanonicalFormKind.Any))
                    {
                        allCanon = false;
                        break;
                    }
                }
                if (allCanon)
                    return; // Already handled by library root provider

                owningTypeIsConcreteGeneric = true;
            }

            // -- Check method-level instantiation (generic methods) --
            bool methodIsConcreteGeneric = false;

            if (method.HasInstantiation && !method.IsGenericMethodDefinition)
            {
                if (method.Instantiation.ContainsSignatureVariables())
                    return;

                bool allCanon = true;
                foreach (TypeDesc arg in method.Instantiation)
                {
                    if (!arg.IsCanonicalDefinitionType(CanonicalFormKind.Any))
                    {
                        allCanon = false;
                        break;
                    }
                }
                if (allCanon && !owningTypeIsConcreteGeneric)
                    return;

                methodIsConcreteGeneric = true;
            }

            // Must have at least one concrete generic dimension
            if (!owningTypeIsConcreteGeneric && !methodIsConcreteGeneric)
                return;

            // Skip methods that have no compilable body
            if (method.IsAbstract || method.IsInternalCall)
                return;

            discoveredMethods.Add(method);
        }
    }
}
