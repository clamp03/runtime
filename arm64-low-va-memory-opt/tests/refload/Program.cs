// Minimal probe for the FEATURE_COMPRESSED_REFS codegen change: each method below is one shape of
// heap reference access, so a JitDisasm dump shows whether the load/store was narrowed to 4 bytes.
using System;
using System.Runtime.CompilerServices;

internal sealed class Node { public object Next; public int Tag; }

internal static class Program
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static object LoadField(Node n) => n.Next;          // GT_IND over LEA(base, imm)

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static object LoadDeref(ref object r) => r;         // GT_IND over plain register

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static object LoadElem(object[] a, int i) => a[i];  // GT_IND over LEA(base, index*8)

    private static int Main()
    {
        var n = new Node { Next = "field" };
        object local = "deref";
        var arr = new object[] { "elem0", "elem1" };
        Console.WriteLine($"{LoadField(n)} {LoadDeref(ref local)} {LoadElem(arr, 1)}");
        return 100;
    }
}
