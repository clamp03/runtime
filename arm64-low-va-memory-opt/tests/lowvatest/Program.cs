// Verifies the Phase A invariant: with DOTNET_GCLowVirtualAddress enabled, every GC heap
// address must fall below 4 GB. Pinned object addresses are real GC heap addresses, so they
// are used as the probe. Also dumps the process's anonymous mappings for context.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

internal static class Program
{
    private const ulong FourGB = 0x1_0000_0000UL;

    private static int Main()
    {
        string mode = Environment.GetEnvironmentVariable("DOTNET_GCLowVirtualAddress") ?? "(unset)";
        Console.WriteLine($"DOTNET_GCLowVirtualAddress = {mode}");
        Console.WriteLine($"IntPtr.Size               = {IntPtr.Size}");
        Console.WriteLine();

        var handles = new List<GCHandle>();
        ulong maxAddr = 0;
        int aboveCount = 0;
        int probes = 0;

        // Sizes chosen to land in gen0, gen2 and the large object heap so that several
        // different GC reservations get exercised.
        int[] sizes = { 64, 4 * 1024, 200 * 1024, 4 * 1024 * 1024, 32 * 1024 * 1024 };

        foreach (int size in sizes)
        {
            // A few allocations per size class to push the heap forward.
            for (int i = 0; i < 4; i++)
            {
                byte[] buf = GC.AllocateUninitializedArray<byte>(size, pinned: false);
                buf[0] = 1;
                buf[buf.Length - 1] = 2;

                GCHandle h = GCHandle.Alloc(buf, GCHandleType.Pinned);
                handles.Add(h);

                ulong addr = (ulong)h.AddrOfPinnedObject().ToInt64();
                probes++;
                if (addr > maxAddr) maxAddr = addr;
                if (addr >= FourGB) aboveCount++;

                if (i == 0)
                {
                    Console.WriteLine($"  size {size,10}  addr 0x{addr:x}  {(addr < FourGB ? "low" : "HIGH")}");
                }
            }
        }

        // Force some GC activity so that segments/bookkeeping are actually established.
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        Console.WriteLine();
        Console.WriteLine($"probes            = {probes}");
        Console.WriteLine($"max GC heap addr  = 0x{maxAddr:x}");
        Console.WriteLine($"addrs >= 4GB      = {aboveCount}");

        DumpLargeAnonMappings();

        foreach (GCHandle h in handles) h.Free();

        bool pass = aboveCount == 0;
        Console.WriteLine();
        Console.WriteLine(pass ? "RESULT: all GC heap probes below 4GB" : "RESULT: some GC heap probes ABOVE 4GB");
        return pass ? 100 : 1;
    }

    // Prints the largest anonymous mappings, which is where GC reservations show up.
    private static void DumpLargeAnonMappings()
    {
        const string path = "/proc/self/maps";
        if (!File.Exists(path)) return;

        var rows = new List<(ulong start, ulong end, string perms)>();
        foreach (string line in File.ReadLines(path))
        {
            // format: start-end perms offset dev inode [path]
            int dash = line.IndexOf('-');
            if (dash <= 0) continue;
            int sp = line.IndexOf(' ', dash);
            if (sp <= 0) continue;

            if (!ulong.TryParse(line.AsSpan(0, dash), System.Globalization.NumberStyles.HexNumber, null, out ulong start)) continue;
            if (!ulong.TryParse(line.AsSpan(dash + 1, sp - dash - 1), System.Globalization.NumberStyles.HexNumber, null, out ulong end)) continue;

            string rest = line.Substring(sp + 1);
            string[] parts = rest.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            // anonymous == no path field
            bool anon = parts.Length < 5;
            if (anon && (end - start) >= (16UL << 20))
            {
                rows.Add((start, end, parts.Length > 0 ? parts[0] : "?"));
            }
        }

        Console.WriteLine();
        Console.WriteLine("large anonymous mappings (>=16MB):");
        rows.Sort((a, b) => (b.end - b.start).CompareTo(a.end - a.start));
        int shown = 0;
        foreach (var r in rows)
        {
            Console.WriteLine($"  0x{r.start:x12}-0x{r.end:x12} {(r.end - r.start) >> 20,6} MB {r.perms} {(r.end <= FourGB ? "" : "  <-- above 4GB")}");
            if (++shown >= 12) break;
        }
    }
}
