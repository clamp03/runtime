// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections.Generic;
using System.Diagnostics;

using ILCompiler.DependencyAnalysis;
using ILCompiler.DependencyAnalysis.ReadyToRun;
using ILCompiler.DependencyAnalysisFramework;

namespace ILCompiler
{
    /// <summary>
    /// Emission-time relaxation of optimistic hard-bind (direct call) edges.
    ///
    /// During compilation, eligible calls to methods within the compilation set are
    /// optimistically emitted as direct-call relocations to the callee's
    /// <see cref="MethodWithGCInfo"/> body, with a MethodPrepare anchor recorded in the
    /// caller's fixup list and a per-callee fallback stub kept alive (see
    /// <see cref="HardBindEdge"/>). Only here — after all methods have been compiled and
    /// the final emitted set is known — can each edge be safely resolved:
    ///
    ///  * KEEP the direct call when the callee actually produced code, keeping the
    ///    MethodPrepare anchor so the runtime prepares the callee before the caller's
    ///    entry point is published (activation + the callee's own fixup list, which
    ///    recursively prepares its direct callees + entry point registration for GC/EH
    ///    code identity).
    ///
    ///  * REDIRECT the relocation to the callee's fallback stub (which routes through the
    ///    callee's regular import cell, preserving the full lazy delay-load protocol) when
    ///    the callee compiled to an empty body, when the edge participates in a
    ///    direct-call cycle (runtime MethodPrepare recursion would not terminate), or when
    ///    the prepare-chain depth exceeds a cap (bounds runtime recursion depth during
    ///    caller preparation). Redirected edges also drop the caller's MethodPrepare
    ///    anchor.
    /// </summary>
    internal static class HardBindRelaxation
    {
        /// <summary>
        /// Upper bound on the direct-call chain depth kept as direct calls. The runtime
        /// prepares kept callees recursively through their fixup lists while preparing
        /// the outermost caller; this cap bounds that native recursion.
        /// </summary>
        private const int MaxPrepareChainDepth = 100;

        public static void Apply(IEnumerable<DependencyNodeCore<NodeFactory>> markedNodes, NodeFactory factory, Logger logger)
        {
            // Gather all callers carrying optimistic edges.
            List<MethodWithGCInfo> callers = new List<MethodWithGCInfo>();
            foreach (DependencyNodeCore<NodeFactory> node in markedNodes)
            {
                if (node is MethodWithGCInfo method && method.HardBindEdges != null && !method.IsEmpty)
                {
                    callers.Add(method);
                }
            }

            if (callers.Count == 0)
            {
                return;
            }

            // Build the graph of candidate direct-call edges whose callee produced real
            // code. Edges to empty callees are redirected outright.
            Dictionary<MethodWithGCInfo, int> nodeIds = new Dictionary<MethodWithGCInfo, int>();
            List<MethodWithGCInfo> nodesById = new List<MethodWithGCInfo>();

            int GetId(MethodWithGCInfo m)
            {
                if (!nodeIds.TryGetValue(m, out int id))
                {
                    id = nodesById.Count;
                    nodeIds.Add(m, id);
                    nodesById.Add(m);
                }
                return id;
            }

            foreach (MethodWithGCInfo caller in callers)
            {
                GetId(caller);
                foreach (HardBindEdge edge in caller.HardBindEdges)
                {
                    if (!edge.Callee.IsEmpty)
                    {
                        GetId(edge.Callee);
                    }
                }
            }

            int nodeCount = nodesById.Count;
            List<int>[] successors = new List<int>[nodeCount];
            foreach (MethodWithGCInfo caller in callers)
            {
                int callerId = nodeIds[caller];
                foreach (HardBindEdge edge in caller.HardBindEdges)
                {
                    if (!edge.Callee.IsEmpty)
                    {
                        (successors[callerId] ??= new List<int>()).Add(nodeIds[edge.Callee]);
                    }
                }
            }

            // Iterative Tarjan SCC. SCCs are emitted in reverse topological order, so a
            // node's prepare-chain depth can be finalized as soon as its SCC is emitted.
            int[] sccOf = new int[nodeCount];
            int[] sccSize;
            int sccCount;
            {
                const int Unvisited = -1;
                int[] index = new int[nodeCount];
                int[] lowlink = new int[nodeCount];
                bool[] onStack = new bool[nodeCount];
                for (int i = 0; i < nodeCount; i++)
                {
                    index[i] = Unvisited;
                    sccOf[i] = Unvisited;
                }

                int nextIndex = 0;
                sccCount = 0;
                List<int> sccSizes = new List<int>();
                Stack<int> tarjanStack = new Stack<int>();
                // Explicit DFS stack: (node, next successor ordinal to visit)
                Stack<(int Node, int NextSucc)> dfs = new Stack<(int, int)>();

                for (int root = 0; root < nodeCount; root++)
                {
                    if (index[root] != Unvisited)
                    {
                        continue;
                    }

                    dfs.Push((root, 0));
                    index[root] = lowlink[root] = nextIndex++;
                    tarjanStack.Push(root);
                    onStack[root] = true;

                    while (dfs.Count > 0)
                    {
                        (int node, int nextSucc) = dfs.Pop();
                        List<int> succ = successors[node];
                        bool descended = false;

                        for (int s = nextSucc; succ != null && s < succ.Count; s++)
                        {
                            int target = succ[s];
                            if (index[target] == Unvisited)
                            {
                                dfs.Push((node, s + 1));
                                dfs.Push((target, 0));
                                index[target] = lowlink[target] = nextIndex++;
                                tarjanStack.Push(target);
                                onStack[target] = true;
                                descended = true;
                                break;
                            }
                            else if (onStack[target] && index[target] < lowlink[node])
                            {
                                lowlink[node] = index[target];
                            }
                        }

                        if (descended)
                        {
                            continue;
                        }

                        if (lowlink[node] == index[node])
                        {
                            int size = 0;
                            int member;
                            do
                            {
                                member = tarjanStack.Pop();
                                onStack[member] = false;
                                sccOf[member] = sccCount;
                                size++;
                            }
                            while (member != node);
                            sccSizes.Add(size);
                            sccCount++;
                        }

                        if (dfs.Count > 0)
                        {
                            (int parent, int parentNext) = dfs.Peek();
                            if (lowlink[node] < lowlink[parent])
                            {
                                lowlink[parent] = lowlink[node];
                            }
                        }
                    }
                }

                sccSize = sccSizes.ToArray();
            }

            // Prepare-chain depth per SCC over kept (inter-SCC) edges. SCC ids are in
            // reverse topological order: all successors of a node belong to SCCs with
            // smaller ids, so a single pass in SCC id order finalizes depths.
            int[] sccDepth = new int[sccCount];
            {
                // Seed with 1 (the callee itself); intra-SCC and empty-callee edges are
                // not in the graph or get redirected, so they do not contribute.
                for (int i = 0; i < sccCount; i++)
                {
                    sccDepth[i] = 1;
                }

                // Nodes must be processed so that successors (smaller SCC id) are final.
                // Iterate nodes grouped by ascending SCC id.
                List<int>[] sccMembers = new List<int>[sccCount];
                for (int n = 0; n < nodeCount; n++)
                {
                    (sccMembers[sccOf[n]] ??= new List<int>()).Add(n);
                }

                for (int scc = 0; scc < sccCount; scc++)
                {
                    if (sccSize[scc] > 1)
                    {
                        continue; // cyclic SCCs: all their outgoing direct edges get redirected below
                    }
                    foreach (int n in sccMembers[scc])
                    {
                        List<int> succ = successors[n];
                        if (succ == null)
                        {
                            continue;
                        }
                        foreach (int target in succ)
                        {
                            int targetScc = sccOf[target];
                            if (targetScc != scc && sccSize[targetScc] == 1)
                            {
                                Debug.Assert(targetScc < scc);
                                int depthThroughTarget = sccDepth[targetScc] + 1;
                                if (depthThroughTarget > sccDepth[scc])
                                {
                                    sccDepth[scc] = depthThroughTarget;
                                }
                            }
                        }
                    }
                }
            }

            // Resolve every edge: rewrite redirected relocations and drop their anchors.
            int keptEdges = 0;
            int redirectedEdges = 0;
            HashSet<MethodWithGCInfo> redirectedCallees = new HashSet<MethodWithGCInfo>();
            HashSet<ISymbolNode> anchorsToRemove = new HashSet<ISymbolNode>();
            Dictionary<MethodWithGCInfo, HardBindStubNode> redirectMap = new Dictionary<MethodWithGCInfo, HardBindStubNode>();

            foreach (MethodWithGCInfo caller in callers)
            {
                redirectedCallees.Clear();
                anchorsToRemove.Clear();
                redirectMap.Clear();

                int callerScc = sccOf[nodeIds[caller]];

                foreach (HardBindEdge edge in caller.HardBindEdges)
                {
                    bool redirect;
                    if (edge.Callee.IsEmpty)
                    {
                        // Callee produced no code in this image: a direct relocation would dangle.
                        redirect = true;
                    }
                    else
                    {
                        int calleeScc = sccOf[nodeIds[edge.Callee]];
                        if (calleeScc == callerScc || sccSize[calleeScc] > 1 || sccSize[callerScc] > 1)
                        {
                            // Direct-call cycle: runtime MethodPrepare recursion would not terminate.
                            redirect = true;
                        }
                        else
                        {
                            // Bound the runtime prepare recursion depth.
                            redirect = sccDepth[calleeScc] >= MaxPrepareChainDepth;
                        }
                    }

                    if (redirect)
                    {
                        if (redirectedCallees.Add(edge.Callee))
                        {
                            redirectMap[edge.Callee] = edge.Stub;
                        }
                        anchorsToRemove.Add(edge.Anchor);
                        redirectedEdges++;
                    }
                    else
                    {
                        keptEdges++;
                    }
                }

                if (redirectMap.Count == 0)
                {
                    continue;
                }

                RewriteRelocations(caller.GetData(factory, relocsOnly: true).Relocs, redirectMap);
                if (caller.ColdCodeNode != null)
                {
                    RewriteRelocations(caller.ColdCodeNode.GetData(factory, relocsOnly: true).Relocs, redirectMap);
                }

                foreach (ISymbolNode anchor in anchorsToRemove)
                {
                    while (caller.Fixups.Remove(anchor))
                    {
                    }
                }
            }

            logger.LogMessage($"Hard bind: {keptEdges} direct call edges kept, {redirectedEdges} redirected to fallback stubs");
        }

        private static void RewriteRelocations(Relocation[] relocations, Dictionary<MethodWithGCInfo, HardBindStubNode> redirectMap)
        {
            if (relocations == null)
            {
                return;
            }

            for (int i = 0; i < relocations.Length; i++)
            {
                if (relocations[i].Target is MethodWithGCInfo callee
                    && redirectMap.TryGetValue(callee, out HardBindStubNode stub))
                {
                    relocations[i] = new Relocation(relocations[i].RelocType, relocations[i].Offset, stub);
                }
            }
        }
    }
}
