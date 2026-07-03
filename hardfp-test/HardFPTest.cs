// armel/SOFTFP hard-float experiment test harness.
//
// Purpose: exercise every managed<->managed and managed<->native floating-point calling-convention path so
// that the SAME binary can be run twice on the armel device:
//     corerun HardFPTest.dll                       # baseline (DOTNET_JitManagedHardFP unset/0)
//     DOTNET_JitManagedHardFP=1 corerun HardFPTest.dll   # experiment
// and the RESULTS must be identical (correctness), while the codegen differs (fewer vmov on managed calls).
//
// Each test computes a value and compares against a constant expected result. Any mismatch => FAIL and the
// process exits non-zero. Tests are grouped by which ABI boundary they stress so that, when bringing the
// experiment up on-device, it is obvious which category first breaks (e.g. delegates/reflection need the
// VM-side ArgIterator change; direct managed calls should work with the JIT-only change).

using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

internal static class HardFPTest
{
    private static int s_failures;
    private const float  EPS_F = 1e-4f;
    private const double EPS_D = 1e-9;

    // NOTE: print INTEGER-scaled values only. Float/double ToString() goes through the number-formatting
    // path (which reaches native FCall/QCall helpers). Under the experiment those are a known gap, so we
    // avoid formatting floats here and instead compare/print scaled integers to check the VALUES themselves.
    private static void CheckF(string name, float actual, float expected)
    {
        long a = (long)(actual * 1000.0 + (actual < 0 ? -0.5 : 0.5));
        long e = (long)(expected * 1000.0 + (expected < 0 ? -0.5 : 0.5));
        bool ok = a == e;
        Console.WriteLine($"[{(ok ? "PASS" : "FAIL")}] {name}: got*1000={a}, expected*1000={e}");
        if (!ok) s_failures++;
    }

    private static void CheckD(string name, double actual, double expected)
    {
        long a = (long)(actual * 1000.0 + (actual < 0 ? -0.5 : 0.5));
        long e = (long)(expected * 1000.0 + (expected < 0 ? -0.5 : 0.5));
        bool ok = a == e;
        Console.WriteLine($"[{(ok ? "PASS" : "FAIL")}] {name}: got*1000={a}, expected*1000={e}");
        if (!ok) s_failures++;
    }

    // ---- 1. Direct (non-virtual) managed calls: float/double scalar args + returns ----------------------
    // These should work end-to-end with the JIT-only change (both sides re-classified hard-float).

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static float AddF(float a, float b) => a + b;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static double AddD(double a, double b) => a + b;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static float MixArgsF(int i, float a, long l, double d, float b)
        => i + a + l + (float)d + b; // mixed int/long/float/double signature -> r0.. + s0.. interleave

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static double ChainD(double x)
    {
        // return value consumed by FP math + FP-computed arg passed on: stresses the boundary vmov both ways.
        double r = AddD(x, 1.0);
        return AddD(r * 2.0, x);
    }

    private static void TestDirectScalars()
    {
        Console.WriteLine("-- 1. direct managed scalar float/double calls --");
        CheckF("AddF", AddF(1.5f, 2.25f), 3.75f);
        CheckD("AddD", AddD(1.5, 2.25), 3.75);
        CheckF("MixArgs", MixArgsF(3, 1.5f, 4L, 0.5, 2.0f), 11.0f);
        CheckD("ChainD", ChainD(2.0), 8.0); // r=3, (3*2)+2 = 8
    }

    // ---- 2. Virtual / interface float calls ------------------------------------------------------------
    // VSD/precode stubs generally preserve arg registers, so these may work; if they crash under the
    // experiment it points at a shuffle thunk needing the VM change.

    private interface IScaler { float Scale(float v); }
    private class Doubler : IScaler { public virtual float Scale(float v) => v * 2.0f; }
    private class Tripler : Doubler { public override float Scale(float v) => v * 3.0f; }

    private static void TestVirtualInterface()
    {
        Console.WriteLine("-- 2. virtual / interface float calls --");
        Doubler d = new Tripler();
        CheckF("virtual", d.Scale(4.0f), 12.0f);
        IScaler s = new Doubler();
        CheckF("interface", s.Scale(4.0f), 8.0f);
    }

    // ---- 3. Delegate float calls -----------------------------------------------------------------------
    // Delegate shuffle thunks use the VM ArgIterator -> expected to need the VM-side change.

    private static void TestDelegates()
    {
        Console.WriteLine("-- 3. delegate float calls --");
        Func<float, float, float> f = AddF;
        CheckF("delegate<float>", f(1.5f, 2.25f), 3.75f);
        Func<double, double, double> g = AddD;
        CheckD("delegate<double>", g(1.5, 2.25), 3.75);
    }

    // ---- 4. HFA-like struct by value (Vector2-ish) -----------------------------------------------------
    // The JIT-only change keeps structs on the SOFTFP (integer-register) path (HFA is a follow-up requiring
    // CONFIGURABLE_ARM_ABI), so this must remain correct either way.

    private struct V2 { public float X, Y; public V2(float x, float y) { X = x; Y = y; } }               // 2-float HFA -> s0,s1
    private struct V4 { public float X, Y, Z, W; public V4(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; } } // 4-float HFA -> s0-s3
    private struct D2 { public double A, B; public D2(double a, double b) { A = a; B = b; } }             // 2-double HFA -> d0,d1

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static V2 AddV2(V2 a, V2 b) => new V2(a.X + b.X, a.Y + b.Y);
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static V4 AddV4(V4 a, V4 b) => new V4(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static D2 AddD2(D2 a, D2 b) => new D2(a.A + b.A, a.B + b.B);
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static float DotMixed(int tag, V4 v, float scale) => (v.X + v.Y + v.Z + v.W) * scale + tag; // HFA + scalars interleaved

    private static void TestStructByValue()
    {
        Console.WriteLine("-- 4. HFA structs by value (float/double, args + returns) --");
        V2 r2 = AddV2(new V2(1.0f, 2.0f), new V2(3.0f, 4.0f));
        CheckF("V2.X", r2.X, 4.0f); CheckF("V2.Y", r2.Y, 6.0f);
        V4 r4 = AddV4(new V4(1, 2, 3, 4), new V4(10, 20, 30, 40));
        CheckF("V4.X", r4.X, 11.0f); CheckF("V4.W", r4.W, 44.0f);
        D2 rd = AddD2(new D2(1.5, 2.5), new D2(3.0, 4.0));
        CheckD("D2.A", rd.A, 4.5); CheckD("D2.B", rd.B, 6.5);
        CheckF("HFA+scalars", DotMixed(100, new V4(1, 2, 3, 4), 2.0f), 120.0f); // (1+2+3+4)*2+100 = 120
    }

    // ---- 5. JIT helpers with float/double signatures ---------------------------------------------------
    // '%' on float/double emits CORINFO_HELP_FLTREM/DBLREM; (long)double emits DBL2LNG. These are direct
    // native (SOFTFP) helper calls -> must stay SOFTFP (IsHelperCall() keeps them so under the experiment).

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static double DRem(double a, double b) => a % b;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static float FRem(float a, float b) => a % b;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static long DblToLng(double d) => (long)d;

    private static void TestHelpers()
    {
        Console.WriteLine("-- 5. float/double JIT helper calls --");
        CheckD("DBLREM", DRem(7.5, 2.0), 1.5);
        CheckF("FLTREM", FRem(7.5f, 2.0f), 1.5f);
        long l = DblToLng(123456.789);
        bool ok = l == 123456L;
        Console.WriteLine($"[{(ok ? "PASS" : "FAIL")}] DBL2LNG: got {l}, expected 123456");
        if (!ok) s_failures++;
    }

    // ---- 6. Forward P/Invoke into libc (managed -> native SOFTFP) --------------------------------------

    [DllImport("libm.so.6", EntryPoint = "fabs")]
    private static extern double c_fabs(double x);

    [DllImport("libm.so.6", EntryPoint = "fmod")]
    private static extern double c_fmod(double x, double y);

    [DllImport("libm.so.6", EntryPoint = "fabsf")]
    private static extern float c_fabsf(float x);

    private static void TestForwardPInvoke()
    {
        Console.WriteLine("-- 6. forward P/Invoke into libc (native SOFTFP boundary) --");
        try
        {
            CheckD("fabs", c_fabs(-3.5), 3.5);
            CheckD("fmod", c_fmod(7.5, 2.0), 1.5);
            CheckF("fabsf", c_fabsf(-2.25f), 2.25f);
        }
        catch (Exception e) { Console.WriteLine($"[FAIL] P/Invoke threw: {e.Message}"); s_failures++; }
    }

    // ---- 7. Unmanaged calli + reverse P/Invoke (both native boundaries, no custom .so) ------------------
    // Calling through the function pointer is an unmanaged (SOFTFP) indirect call; entering RevAddF is a
    // reverse-P/Invoke transition (also SOFTFP). Both boundaries must stay SOFTFP under the experiment.

    [UnmanagedCallersOnly]
    private static float RevAddF(float a, float b) => a + b;

    [UnmanagedCallersOnly]
    private static double RevAddD(double a, double b) => a + b;

    private static unsafe void TestUnmanagedCallersOnly()
    {
        Console.WriteLine("-- 7. unmanaged calli + reverse P/Invoke --");
        try
        {
            delegate* unmanaged<float, float, float> pf = &RevAddF;
            CheckF("revpinvoke<float>", pf(1.5f, 2.25f), 3.75f);
            delegate* unmanaged<double, double, double> pd = &RevAddD;
            CheckD("revpinvoke<double>", pd(1.5, 2.25), 3.75);
        }
        catch (Exception e) { Console.WriteLine($"[FAIL] UnmanagedCallersOnly threw: {e.Message}"); s_failures++; }
    }

    // ---- 8. Reflection Invoke of a float method (CallDescrWorker path) ---------------------------------

    private static void TestReflection()
    {
        Console.WriteLine("-- 8. reflection Invoke of float/double method --");
        try
        {
            MethodInfo mf = typeof(HardFPTest).GetMethod(nameof(AddF), BindingFlags.NonPublic | BindingFlags.Static);
            object rf = mf.Invoke(null, new object[] { 1.5f, 2.25f });
            CheckF("reflect<float>", (float)rf, 3.75f);
            MethodInfo md = typeof(HardFPTest).GetMethod(nameof(AddD), BindingFlags.NonPublic | BindingFlags.Static);
            object rd = md.Invoke(null, new object[] { 1.5, 2.25 });
            CheckD("reflect<double>", (double)rd, 3.75);
        }
        catch (Exception e) { Console.WriteLine($"[FAIL] Reflection threw: {e.Message}"); s_failures++; }
    }

    // ---- 9. Hot loop calling a float leaf (cumulative boundary cost) -----------------------------------

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static float Kernel(float acc, float x) => acc + x * x;

    private static void TestHotLoop()
    {
        Console.WriteLine("-- 9. hot loop of float leaf calls --");
        float acc = 0f;
        for (int i = 1; i <= 100; i++) acc = Kernel(acc, i);
        // sum of squares 1..100 = 338350
        CheckF("sumSquares", acc, 338350.0f);
    }

    // ---- 10. Float-signature FCalls (Math.*) ----------------------------------------------------------
    // These reach native FCalls compiled -mfloat-abi=softfp. Under the experiment they must be classified
    // SOFTFP by the JIT (via GTF_CALL_M_NOGCCHECK) or the double arg/return is mangled. Validated as integers.

    private static void TestFCallMath()
    {
        Console.WriteLine("-- 10. float-signature FCalls (Math.*) --");
        CheckLong("Math.Round", (long)Math.Round(2.7), 3);
        CheckLong("Math.Floor", (long)Math.Floor(2.7), 2);
        CheckLong("Math.Ceiling", (long)Math.Ceiling(2.3), 3);
        CheckLong("Math.Pow", (long)Math.Pow(2.0, 10.0), 1024);
        CheckLong("Math.Sqrt", (long)Math.Sqrt(144.0), 12);
    }

    private static void CheckLong(string name, long actual, long expected)
    {
        bool ok = actual == expected;
        Console.WriteLine($"[{(ok ? "PASS" : "FAIL")}] {name}: got={actual}, expected={expected}");
        if (!ok) s_failures++;
    }

    private static int Main()
    {
        Console.WriteLine($"HardFPTest  (DOTNET_JitManagedHardFP={Environment.GetEnvironmentVariable("DOTNET_JitManagedHardFP") ?? "<unset>"})");
        Console.WriteLine($"RuntimeInformation: {RuntimeInformation.ProcessArchitecture} / {RuntimeInformation.FrameworkDescription}");
        Console.WriteLine();

        TestDirectScalars();
        TestVirtualInterface();
        TestDelegates();
        TestStructByValue();
        TestHelpers();
        TestForwardPInvoke();
        TestUnmanagedCallersOnly();
        TestReflection();
        TestHotLoop();
        TestFCallMath();

        Console.WriteLine();
        Console.WriteLine(s_failures == 0 ? "ALL TESTS PASSED" : $"{s_failures} TEST(S) FAILED");
        return s_failures == 0 ? 0 : 1;
    }
}
