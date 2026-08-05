// Functional stress check for the low-VA GC reservation change: allocates across all size
// classes from several threads, survives multiple GCs, and verifies data integrity so that
// a mis-mapped or overlapping reservation would show up as corruption rather than silence.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

internal static class Program
{
    private const ulong FourGB = 0x1_0000_0000UL;

    private static int Main()
    {
        Console.WriteLine($"GCLowVirtualAddress={Environment.GetEnvironmentVariable("DOTNET_GCLowVirtualAddress") ?? "(unset)"} " +
                          $"GCRegionRange={Environment.GetEnvironmentVariable("DOTNET_GCRegionRange") ?? "(unset)"}");

        var sw = Stopwatch.StartNew();
        int errors = 0;

        // Multi-threaded allocation with integrity verification.
        int threads = Math.Min(8, Environment.ProcessorCount);
        Parallel.For(0, threads, t =>
        {
            var rng = new Random(1000 + t);
            var live = new List<byte[]>();

            for (int iter = 0; iter < 4000; iter++)
            {
                // Mix of gen0-sized, gen2-sized and LOH-sized allocations.
                int size = rng.Next(100) switch
                {
                    < 70 => rng.Next(16, 2048),           // small
                    < 90 => rng.Next(2048, 90_000),       // medium
                    < 98 => rng.Next(90_000, 500_000),    // large / LOH boundary
                    _ => rng.Next(500_000, 3_000_000),    // LOH
                };

                var buf = new byte[size];
                byte tag = (byte)(iter ^ t);
                buf[0] = tag;
                buf[size - 1] = tag;
                buf[size / 2] = tag;
                live.Add(buf);

                // Keep a bounded live set so the heap actually grows then recycles.
                if (live.Count > 200)
                {
                    int drop = rng.Next(live.Count);
                    var victim = live[drop];
                    byte vt = victim[0];
                    if (victim[victim.Length - 1] != vt || victim[victim.Length / 2] != vt)
                    {
                        Interlocked.Increment(ref errors);
                    }
                    live.RemoveAt(drop);
                }

                if ((iter % 500) == 0)
                {
                    // Exercise strings / boxing / dictionaries too.
                    var d = new Dictionary<string, object>();
                    for (int k = 0; k < 50; k++) d["k" + k] = (object)k;
                    if (d.Count != 50) Interlocked.Increment(ref errors);
                }
            }

            // Final integrity sweep of whatever is still live.
            foreach (var b in live)
            {
                byte vt = b[0];
                if (b[b.Length - 1] != vt || b[b.Length / 2] != vt)
                {
                    Interlocked.Increment(ref errors);
                }
            }
        });

        for (int i = 0; i < 3; i++)
        {
            GC.Collect(2, GCCollectionMode.Forced, blocking: true);
            GC.WaitForPendingFinalizers();
        }

        // Weak references + finalizers still behave.
        var wr = AllocWeak();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        if (wr.IsAlive) Console.WriteLine("note: weak target still alive (not fatal)");

        sw.Stop();
        var info = GC.GetGCMemoryInfo();
        Console.WriteLine($"elapsed={sw.ElapsedMilliseconds}ms gen0={GC.CollectionCount(0)} gen1={GC.CollectionCount(1)} gen2={GC.CollectionCount(2)}");
        Console.WriteLine($"heapSize={info.HeapSizeBytes / (1024 * 1024)}MB committed={info.TotalCommittedBytes / (1024 * 1024)}MB totalAllocated={GC.GetTotalAllocatedBytes() / (1024 * 1024)}MB");
        DumpProcMemory();
        Console.WriteLine($"integrity errors = {errors}");
        Console.WriteLine(errors == 0 ? "RESULT: PASS" : "RESULT: FAIL");
        return errors == 0 ? 100 : 1;
    }

    // VmPeak/VmSize show the virtual address cost (reservations); VmHWM/VmRSS show the
    // physical cost (commits). The low-VA work changes the former, so both are reported.
    private static void DumpProcMemory()
    {
        try
        {
            foreach (string line in System.IO.File.ReadLines("/proc/self/status"))
            {
                if (line.StartsWith("VmPeak") || line.StartsWith("VmSize") ||
                    line.StartsWith("VmHWM") || line.StartsWith("VmRSS"))
                {
                    Console.WriteLine("  " + line.Replace("\t", " "));
                }
            }
        }
        catch (System.IO.IOException) { }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference AllocWeak()
    {
        var o = new byte[1024];
        return new WeakReference(o);
    }
}
