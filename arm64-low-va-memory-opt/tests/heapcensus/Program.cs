// Builds a live object graph of a chosen shape and then forces a blocking gen2 GC, which is where
// the runtime's heap census reports from. Run with DOTNET_CompressedPtrHeapCensus=1:
//
//   run-lowva.sh heapcensus.dll DOTNET_CompressedPtrHeapCensus=1 -- <profile>
//
// The point is not these synthetic numbers by themselves — the census works on any process, so the
// real workload should be measured directly. These profiles exist to bracket the answer: they show
// how far apart reference-rich and value-rich workloads land, so a real measurement can be placed
// between them.
using System;
using System.Collections.Generic;
using System.Text;

internal sealed class Node
{
    public Node Left;
    public Node Right;
    public object Tag;
    public int Id;
}

internal static class Program
{
    // Static so the graph stays live across the forced collection.
    private static object s_live;

    private const int Scale = 20000;

    private static object BuildCollections()
    {
        // Reference-rich: dictionary entries, per-entry nodes, cross links.
        var map = new Dictionary<int, Node>(Scale);
        var all = new List<Node>(Scale);
        for (int i = 0; i < Scale; i++)
        {
            var n = new Node { Id = i, Tag = new object() };
            map[i] = n;
            all.Add(n);
        }
        for (int i = 0; i < Scale; i++)
        {
            all[i].Left = all[(i + 1) % Scale];
            all[i].Right = all[(i * 7 + 3) % Scale];
        }
        return new object[] { map, all };
    }

    private static object BuildText()
    {
        // Value-rich: string characters and byte buffers dominate.
        var strings = new List<string>(Scale);
        var buffers = new List<byte[]>(Scale / 10);
        for (int i = 0; i < Scale; i++)
        {
            strings.Add("payload-" + i.ToString("D8") + "-" + new string('x', 24));
        }
        for (int i = 0; i < Scale / 10; i++)
        {
            buffers.Add(new byte[512]);
        }
        return new object[] { strings, buffers };
    }

    private static object BuildArrays()
    {
        // Reference arrays only: the shape B2 helps most (42-49% in sizemeasure).
        var arrays = new object[Scale / 16][];
        for (int i = 0; i < arrays.Length; i++)
        {
            var a = new object[16];
            for (int j = 0; j < 16; j++) a[j] = new object();
            arrays[i] = a;
        }
        return arrays;
    }

    private static object BuildMixed()
    {
        // Roughly what application code looks like: objects, collections, strings and buffers
        // together rather than any single shape.
        var sb = new StringBuilder();
        var rows = new List<object>(Scale);
        for (int i = 0; i < Scale; i++)
        {
            if ((i & 3) == 0)
            {
                rows.Add(new Node { Id = i, Tag = "tag" + i });
            }
            else if ((i & 3) == 1)
            {
                rows.Add("row-" + i.ToString("D6"));
            }
            else if ((i & 3) == 2)
            {
                rows.Add(new int[8]);
            }
            else
            {
                rows.Add(new object[4] { i, "s", new object(), null });
            }
            if ((i & 1023) == 0) sb.Append(i);
        }
        return new object[] { rows, sb, BuildCollections() };
    }

    private static int Main(string[] args)
    {
        string profile = args.Length > 0 ? args[args.Length - 1] : "mixed";

        s_live = profile switch
        {
            // Nothing of our own: measures the framework's own baseline live heap, which on a
            // small-memory device is a large share of the total.
            "startup" => new object(),
            "collections" => BuildCollections(),
            "text" => BuildText(),
            "arrays" => BuildArrays(),
            "mixed" => BuildMixed(),
            _ => null,
        };

        if (s_live is null)
        {
            Console.WriteLine($"unknown profile '{profile}' (startup | collections | text | arrays | mixed)");
            return 101;
        }

        Console.WriteLine($"profile={profile} scale={Scale}");
        Console.WriteLine($"managed heap after build = {GC.GetTotalMemory(false) / 1024} KB");

        // The census reports from the end of a blocking gen2 GC, so trigger one with the graph live.
        GC.Collect(2, GCCollectionMode.Forced, blocking: true);
        GC.WaitForPendingFinalizers();
        GC.Collect(2, GCCollectionMode.Forced, blocking: true);

        GC.KeepAlive(s_live);
        Console.WriteLine("RESULT: PASS");
        return 100;
    }
}
