// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace ILCompiler.DependencyAnalysis.ReadyToRun
{
    /// <summary>
    /// One optimistic direct-call (hard-bind) edge recorded at JIT time. The caller's code
    /// carries a direct-call relocation to <see cref="Callee"/>; whether the relocation is
    /// kept (with <see cref="Anchor"/> in the caller's fixup list) or redirected to
    /// <see cref="Stub"/> is decided by the hard-bind relaxation pass once the full set of
    /// compiled methods is known.
    /// </summary>
    public struct HardBindEdge
    {
        public MethodWithGCInfo Callee;
        public Import Anchor;
        public HardBindStubNode Stub;
    }
}
