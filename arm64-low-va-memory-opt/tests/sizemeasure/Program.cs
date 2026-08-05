// Measures the real per-object footprint of representative object shapes, then projects what
// narrowing the MethodTable slot (Stage B1) and object references (Stage B2) would save.
//
// Sizes are measured, not assumed: GC.GetAllocatedBytesForCurrentThread() before/after a batch of
// identical allocations gives the exact per-instance size the runtime charges.
using System;
using System.Runtime.CompilerServices;

internal sealed class Ref1 { public object A; }
internal sealed class Ref4 { public object A, B, C, D; }
internal sealed class Mixed { public object A; public int I; public object B; public long L; }
internal sealed class Val4 { public int A, B, C, D; }

internal static class Program
{
    private const int Batch = 20000;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static long Measure(Func<object> alloc)
    {
        // Warm up so that any lazy init is not charged to the batch.
        for (int i = 0; i < 100; i++) { GC.KeepAlive(alloc()); }

        var keep = new object[Batch];
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < Batch; i++) keep[i] = alloc();
        long after = GC.GetAllocatedBytesForCurrentThread();
        GC.KeepAlive(keep);
        return (after - before) / Batch;
    }

    private static void Row(string name, long actual, int headerRefs, int payloadRefs, long fixedPayload)
    {
        // Current layout : syncblk(8) + MT(8) + payload, where each reference costs 8.
        // B1 (MT only)   : syncblk(8) + MT(4) + payload   -> saves 4 per object (once padding is gone)
        // B1+B2          : syncblk(8) + MT(4) + payload with each reference costing 4
        long b1 = actual - 4;
        long b12 = actual - 4 - (payloadRefs * 4L);

        // Objects are 8 byte aligned, so round the projections up the way the allocator would.
        b1 = (b1 + 7) & ~7L;
        b12 = (b12 + 7) & ~7L;

        double p1 = 100.0 * (actual - b1) / actual;
        double p12 = 100.0 * (actual - b12) / actual;

        Console.WriteLine($"  {name,-22} actual={actual,5}  B1={b1,5} ({p1,4:F1}%)  B1+B2={b12,5} ({p12,4:F1}%)   refs={payloadRefs}");
    }

    private static int Main()
    {
        Console.WriteLine($"IntPtr.Size={IntPtr.Size}  (measured per-instance bytes, batch={Batch})");
        Console.WriteLine();

        long objSize   = Measure(() => new object());
        long ref1Size  = Measure(() => new Ref1());
        long ref4Size  = Measure(() => new Ref4());
        long mixedSize = Measure(() => new Mixed());
        long val4Size  = Measure(() => new Val4());
        long arr16Ref  = Measure(() => new object[16]);
        long arr16Int  = Measure(() => new int[16]);
        long arr256Ref = Measure(() => new object[256]);
        long str16     = Measure(() => new string('x', 16));

        Console.WriteLine("shape                    현재   ->  B1(MT 4B)        B1+B2(refs 4B)");
        Row("object",            objSize,   1, 0, 0);
        Row("class{1 ref}",      ref1Size,  1, 1, 0);
        Row("class{4 refs}",     ref4Size,  1, 4, 0);
        Row("class{2ref+int+long}", mixedSize, 1, 2, 12);
        Row("class{4 ints}",     val4Size,  1, 0, 16);
        Row("object[16]",        arr16Ref,  1, 16, 0);
        Row("int[16]",           arr16Int,  1, 0, 64);
        Row("object[256]",       arr256Ref, 1, 256, 0);
        Row("string(16)",        str16,     1, 0, 32);

        Console.WriteLine();
        Console.WriteLine("해석:");
        Console.WriteLine("  B1 단독은 객체당 고정 4바이트 -> 작은 객체일수록 비율이 크고, 배열에는 거의 무의미.");
        Console.WriteLine("  B2(참조 4바이트)는 참조가 많은 객체/배열에서 절감이 급격히 커짐.");
        return 100;
    }
}
