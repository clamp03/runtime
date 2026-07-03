// armel hard-float microbenchmark: measures the wall-clock impact of eliminating the SOFTFP boundary vmov
// on managed float call boundaries. Run the SAME binary twice on-device and compare:
//     corerun HardFPBench.dll                          # SOFTFP baseline
//     DOTNET_JitManagedHardFP=1 corerun HardFPBench.dll # hard-float
// Force optimized (Tier1) codegen for a clean measurement:
//     DOTNET_TieredCompilation=0
//
// Each kernel is [NoInlining] so the call boundary (where SOFTFP inserts vmov core<->VFP) is real.

using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;

internal static class HardFPBench
{
    // ---- scalar float: acc + x*x - 0.5*x  (cheap body so the boundary vmov dominates) ----
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static float ScalarF(float acc, float x) => acc + x * x - 0.5f * x;

    // ---- scalar double ----
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static double ScalarD(double acc, double x) => acc + x * x - 0.5 * x;

    // ---- HFA: 4-float struct by value (Vector4-like), passed + returned ----
    private struct V4 { public float X, Y, Z, W; public V4(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; } }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static V4 HfaF(V4 a, V4 b) => new V4(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);

    // ---- HFA: 2-double struct by value ----
    private struct D2 { public double A, B; public D2(double a, double b) { A = a; B = b; } }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static D2 HfaD(D2 a, D2 b) => new D2(a.A + b.A, a.B + b.B);

    private static double BenchScalarF(long iters)
    {
        float acc = 0f;
        var sw = Stopwatch.StartNew();
        for (long i = 0; i < iters; i++)
            acc = ScalarF(acc, (float)(i & 255));
        sw.Stop();
        GC.KeepAlive(acc);
        return sw.Elapsed.TotalMilliseconds;
    }

    private static double BenchScalarD(long iters)
    {
        double acc = 0.0;
        var sw = Stopwatch.StartNew();
        for (long i = 0; i < iters; i++)
            acc = ScalarD(acc, (double)(i & 255));
        sw.Stop();
        GC.KeepAlive(acc);
        return sw.Elapsed.TotalMilliseconds;
    }

    private static double BenchHfaF(long iters)
    {
        V4 acc = new V4(0, 0, 0, 0);
        V4 one = new V4(1, 2, 3, 4);
        var sw = Stopwatch.StartNew();
        for (long i = 0; i < iters; i++)
            acc = HfaF(acc, one);
        sw.Stop();
        GC.KeepAlive(acc.X + acc.W);
        return sw.Elapsed.TotalMilliseconds;
    }

    private static double BenchHfaD(long iters)
    {
        D2 acc = new D2(0, 0);
        D2 one = new D2(1, 2);
        var sw = Stopwatch.StartNew();
        for (long i = 0; i < iters; i++)
            acc = HfaD(acc, one);
        sw.Stop();
        GC.KeepAlive(acc.A + acc.B);
        return sw.Elapsed.TotalMilliseconds;
    }

    private static void Run(string name, Func<long, double> bench, long iters)
    {
        bench(iters / 10);                 // warmup
        double best = double.MaxValue;
        for (int r = 0; r < 5; r++)        // best of 5 to cut noise
            best = Math.Min(best, bench(iters));
        double nsPerCall = best * 1e6 / iters;
        Console.WriteLine($"{name,-14} {best,10:F1} ms   {nsPerCall,7:F3} ns/call   ({iters:N0} calls)");
    }

    private static int Main(string[] args)
    {
        long iters = args.Length > 0 ? long.Parse(args[0]) : 200_000_000;
        Console.WriteLine($"HardFPBench (DOTNET_JitManagedHardFP={Environment.GetEnvironmentVariable("DOTNET_JitManagedHardFP") ?? "<unset>"}, " +
                          $"TieredComp={Environment.GetEnvironmentVariable("DOTNET_TieredCompilation") ?? "<default>"})");
        Console.WriteLine($"{RuntimeInformation()}");
        Console.WriteLine();
        Run("scalar float", BenchScalarF, iters);
        Run("scalar double", BenchScalarD, iters);
        Run("HFA V4(4xf)", BenchHfaF, iters);
        Run("HFA D2(2xd)", BenchHfaD, iters);
        return 0;
    }

    private static string RuntimeInformation()
        => $"{System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture} / {System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription}";
}
