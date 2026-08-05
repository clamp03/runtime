// Exercises every CoreLib consumer of the managed MethodTable mirror in
// RuntimeHelpers.CoreCLR.cs, which FEATURE_COMPRESSED_MT_FIELDS re-lays-out:
//
//   MethodTable.AuxiliaryData   -> StaticsHelpers (GC / non-GC / thread statics),
//                                  InitHelpers (IsClassInited*), Stream override caching,
//                                  ValueType.Equals fast path, RuntimeTypeHandle.ToType
//                                  (ExposedClassObject)
//   MethodTable.ParentMethodTable -> CastHelpers parent-chain walk, RuntimeType enum/delegate checks
//   ElementTypeOffset / InterfaceMapOffset -> array element type handle, interface casts,
//                                  Nullable<T> unbox data
//
// A wrong offset in the mirror shows up here as a wrong answer or a crash rather than as
// something that only manifests deep in an application.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;

internal enum Color { Red, Green, Blue }

internal enum LongColor : long { A = 1L << 40, B = 1L << 41 }

// A static-only class per shape of statics, so each takes a distinct StaticsHelpers path.
internal static class GcStatics
{
    public static string Name = "gc-statics";
    public static int[] Data = new int[8];
}

internal static class NonGcStatics
{
    public static int Count = 41;
    public static long Big = 0x1122334455667788L;
}

internal static class GcStaticsWithCctor
{
    public static readonly string Name;
    static GcStaticsWithCctor() { Name = "cctor-ran"; }
}

internal static class GenericStaticsHolder<T>
{
    public static T Value;
    public static int Hits;
}

internal static class ThreadStatics
{
    [ThreadStatic] public static string GcSlot;
    [ThreadStatic] public static int NonGcSlot;
}

internal static class GenericThreadStatics<T>
{
    [ThreadStatic] public static T GcSlot;
    [ThreadStatic] public static int NonGcSlot;
}

// Deep hierarchy: CastHelpers walks ParentMethodTable one level at a time and bails out to the
// slow helper after a fixed number of hops, so cover both sides of that boundary.
internal class L0 { }
internal class L1 : L0 { }
internal class L2 : L1 { }
internal class L3 : L2 { }
internal class L4 : L3 { }
internal class L5 : L4 { }
internal class L6 : L5 { }
internal class L7 : L6 { }

internal interface IMarker { }
internal interface IOther { }
internal sealed class Marked : L7, IMarker { }

internal struct Blittable : IEquatable<Blittable>
{
    public int A;
    public long B;
    public bool Equals(Blittable other) => A == other.A && B == other.B;
}

// No IEquatable and no override: ValueType.Equals / GetHashCode take the
// CanCompareBitsOrUseFastGetHashCode path, which reads AuxiliaryData flags.
internal struct BitwiseComparable
{
    public int A;
    public short B;
    public byte C;
}

internal struct HasReference
{
    public string S;
    public int I;
}

internal sealed class PlainStream : MemoryStream
{
    public PlainStream(byte[] b) : base(b) { }
}

// Overrides Read/Write, so Stream's override cache (AuxiliaryData flags) must report "overridden".
internal sealed class OverridingStream : Stream
{
    private readonly MemoryStream _inner = new MemoryStream();
    public int Reads, Writes;

    public override bool CanRead => true;
    public override bool CanSeek => true;
    public override bool CanWrite => true;
    public override long Length => _inner.Length;
    public override long Position { get => _inner.Position; set => _inner.Position = value; }
    public override void Flush() => _inner.Flush();
    public override long Seek(long o, SeekOrigin w) => _inner.Seek(o, w);
    public override void SetLength(long v) => _inner.SetLength(v);

    public override int Read(byte[] buffer, int offset, int count)
    {
        Reads++;
        return _inner.Read(buffer, offset, count);
    }

    public override void Write(byte[] buffer, int offset, int count)
    {
        Writes++;
        _inner.Write(buffer, offset, count);
    }
}

internal delegate int Adder(int a, int b);

internal static class Program
{
    private static int s_errors;

    private static void Check(bool cond, string what)
    {
        if (!cond) { s_errors++; Console.WriteLine("  FAIL: " + what); }
    }

    private static int Main()
    {
        Console.WriteLine("MethodTable managed-mirror stress (statics / aux data / parent chain)");

        Statics();
        ThreadStaticFields();
        ExposedClassObjects();
        ParentChain();
        ValueTypeComparison();
        StreamOverrides();
        EnumAndDelegateShapes();
        NullableAndArrayShapes();

        Console.WriteLine("errors = " + s_errors);
        Console.WriteLine(s_errors == 0 ? "RESULT: PASS" : "RESULT: FAIL");
        return s_errors == 0 ? 100 : 101;
    }

    // StaticsHelpers.GetGCStaticBase / GetNonGCStaticBase / GetDynamicStaticsInfo, and
    // InitHelpers.IsClassInited* — all reached through MethodTable.AuxiliaryData.
    private static void Statics()
    {
        Check(GcStatics.Name == "gc-statics", "gc static string");
        GcStatics.Data[3] = 7;
        Check(GcStatics.Data[3] == 7, "gc static array");

        Check(NonGcStatics.Count == 41, "non-gc static int");
        NonGcStatics.Count++;
        Check(NonGcStatics.Count == 42, "non-gc static int write");
        Check(NonGcStatics.Big == 0x1122334455667788L, "non-gc static long");

        // Class with an explicit cctor: forces the "not yet initialized" branch first.
        Check(GcStaticsWithCctor.Name == "cctor-ran", "static cctor ran");

        // Generic statics use GenericsStaticsInfo, a different negative-offset structure in
        // front of MethodTableAuxiliaryData.
        GenericStaticsHolder<string>.Value = "s";
        GenericStaticsHolder<int>.Value = 5;
        GenericStaticsHolder<object>.Value = new object();
        for (int i = 0; i < 100; i++)
        {
            GenericStaticsHolder<string>.Hits++;
            GenericStaticsHolder<int>.Hits += 2;
        }
        Check(GenericStaticsHolder<string>.Value == "s", "generic gc static");
        Check(GenericStaticsHolder<int>.Value == 5, "generic non-gc static");
        Check(GenericStaticsHolder<object>.Value != null, "generic object static");
        Check(GenericStaticsHolder<string>.Hits == 100 && GenericStaticsHolder<int>.Hits == 200,
              "generic static counters");

        // Statics of types only known reflectively still go through the same helpers.
        FieldInfo fi = typeof(NonGcStatics).GetField("Count");
        Check(fi != null && (int)fi.GetValue(null) == 42, "reflected static read");
    }

    // StaticsHelpers.GetThreadStaticBase — ThreadStaticsInfo, another negative-offset structure.
    private static void ThreadStaticFields()
    {
        ThreadStatics.GcSlot = "main";
        ThreadStatics.NonGcSlot = 1;
        GenericThreadStatics<string>.GcSlot = "main-generic";
        GenericThreadStatics<string>.NonGcSlot = 2;

        string observed = null;
        int observedInt = -1;
        var t = new Thread(() =>
        {
            // A fresh thread must see the default, not the main thread's value.
            observed = ThreadStatics.GcSlot;
            observedInt = ThreadStatics.NonGcSlot;
            ThreadStatics.GcSlot = "worker";
            ThreadStatics.NonGcSlot = 99;
            GenericThreadStatics<string>.GcSlot = "worker-generic";
            Check(ThreadStatics.GcSlot == "worker", "thread static write on worker");
            Check(GenericThreadStatics<string>.GcSlot == "worker-generic", "generic thread static on worker");
        });
        t.Start();
        t.Join();

        Check(observed == null, "thread static isolated (gc slot)");
        Check(observedInt == 0, "thread static isolated (non-gc slot)");
        Check(ThreadStatics.GcSlot == "main", "thread static preserved on main");
        Check(ThreadStatics.NonGcSlot == 1, "thread static int preserved on main");
        Check(GenericThreadStatics<string>.GcSlot == "main-generic", "generic thread static on main");
        Check(GenericThreadStatics<string>.NonGcSlot == 2, "generic thread static int on main");
    }

    // RuntimeTypeHandle.ToType reads AuxiliaryData.ExposedClassObject; a wrong offset either
    // returns a bogus object or forces the slow path for every type.
    private static void ExposedClassObjects()
    {
        Type[] types =
        {
            typeof(object), typeof(string), typeof(int), typeof(int[]), typeof(int[,]),
            typeof(Color), typeof(LongColor), typeof(List<string>), typeof(Dictionary<string, int>),
            typeof(Blittable), typeof(HasReference), typeof(Nullable<int>), typeof(Adder),
            typeof(IMarker), typeof(Marked), typeof(GenericStaticsHolder<string>),
        };

        foreach (Type t in types)
        {
            Type again = Type.GetTypeFromHandle(t.TypeHandle);
            Check(ReferenceEquals(t, again), "exposed type identity for " + t.Name);
            Check(t.Name.Length > 0, "type name for " + t.Name);
        }

        // obj.GetType() on many instances, then confirm identity with typeof.
        object[] objs = { new object(), "s", 1, new int[1], Color.Green, new Marked(), new List<string>() };
        foreach (object o in objs)
        {
            Type t = o.GetType();
            Check(ReferenceEquals(t, Type.GetTypeFromHandle(t.TypeHandle)), "GetType identity " + t.Name);
        }

        // Repeat under allocation pressure so the cached handle is exercised after GCs.
        for (int i = 0; i < 200; i++)
        {
            _ = new byte[1024];
            Check(ReferenceEquals(typeof(Marked), new Marked().GetType()), "GetType under GC pressure");
        }
    }

    // CastHelpers walks ParentMethodTable; Marked is 8 levels deep, which straddles the
    // fixed number of inline hops before the slow helper takes over.
    private static void ParentChain()
    {
        object m = new Marked();
        Check(m is L0 && m is L3 && m is L6 && m is L7, "isinst up the chain");
        Check(((L0)m) != null && ((L4)m) != null && ((L7)m) != null, "castclass up the chain");
        Check(m is IMarker, "interface cast");
        Check(!(m is IOther), "negative interface cast");

        object l3 = new L3();
        Check(!(l3 is L4), "negative isinst down the chain");
        try { _ = (L7)l3; Check(false, "invalid downcast not thrown"); }
        catch (InvalidCastException) { }

        // Array covariance store checks also read the element MethodTable's parent chain.
        L0[] arr = new L7[4];
        arr[0] = new Marked();
        Check(arr[0] is Marked, "covariant array store");
        try { arr[1] = new L3(); Check(false, "bad covariant store not thrown"); }
        catch (ArrayTypeMismatchException) { }

        // Base type chain via reflection reads ParentMethodTable too.
        int depth = 0;
        for (Type t = typeof(Marked); t != null; t = t.BaseType) { depth++; }
        Check(depth == 10, "reflected base chain depth = " + depth); // Marked..L0 + object
    }

    // ValueType.Equals / GetHashCode consult AuxiliaryData's
    // CanCompareBitsOrUseFastGetHashCode flags, which are lazily computed and cached.
    private static void ValueTypeComparison()
    {
        object a = new BitwiseComparable { A = 1, B = 2, C = 3 };
        object b = new BitwiseComparable { A = 1, B = 2, C = 3 };
        object c = new BitwiseComparable { A = 1, B = 2, C = 4 };
        Check(a.Equals(b), "bitwise struct equal");
        Check(!a.Equals(c), "bitwise struct not equal");
        Check(a.GetHashCode() == b.GetHashCode(), "bitwise struct hash");

        object r1 = new HasReference { S = "x", I = 1 };
        object r2 = new HasReference { S = "x", I = 1 };
        Check(r1.Equals(r2), "struct with reference field equal");
        Check(!r1.Equals(new HasReference { S = "y", I = 1 }), "struct with reference field not equal");

        // Hitting the flags repeatedly covers both the "not yet checked" and cached branches.
        var set = new HashSet<BitwiseComparable>();
        for (int i = 0; i < 500; i++) { set.Add(new BitwiseComparable { A = i, B = (short)i, C = (byte)i }); }
        Check(set.Count == 500, "struct hash set count = " + set.Count);

        // EqualityComparer<T>.Default construction is what first crashed before the mirror
        // was aligned, so pin it down explicitly for several shapes.
        Check(EqualityComparer<BitwiseComparable>.Default.Equals(
                  new BitwiseComparable { A = 9 }, new BitwiseComparable { A = 9 }), "comparer struct");
        Check(EqualityComparer<string>.Default.Equals("q", "q"), "comparer string");
        Check(EqualityComparer<Color>.Default.Equals(Color.Blue, Color.Blue), "comparer enum");
        Check(EqualityComparer<Blittable>.Default.Equals(new Blittable { A = 1 }, new Blittable { A = 1 }),
              "comparer IEquatable struct");
    }

    // Stream caches "does this subclass override Read/Write" in AuxiliaryData flags.
    private static void StreamOverrides()
    {
        var plainBytes = new byte[] { 1, 2, 3, 4 };
        using (var plain = new PlainStream(plainBytes))
        {
            var buf = new byte[4];
            Check(plain.Read(buf, 0, 4) == 4, "plain stream read count");
            Check(buf[2] == 3, "plain stream read content");
        }

        var over = new OverridingStream();
        over.Write(new byte[] { 7, 8, 9 }, 0, 3);
        over.Position = 0;
        var rbuf = new byte[3];
        Check(over.Read(rbuf, 0, 3) == 3, "overriding stream read count");
        Check(rbuf[0] == 7 && rbuf[2] == 9, "overriding stream content");
        Check(over.Reads == 1 && over.Writes == 1, "overrides actually called");

        // The async wrappers are what consult the cached override flags.
        over.Position = 0;
        Check(over.ReadAsync(rbuf, 0, 3).GetAwaiter().GetResult() == 3, "overriding stream ReadAsync");
        over.Position = 0;
        over.WriteAsync(new byte[] { 1, 2, 3 }, 0, 3).GetAwaiter().GetResult();
        Check(over.Writes >= 2, "overriding stream WriteAsync reached override");

        using (var ms = new MemoryStream())
        {
            ms.WriteAsync(new byte[] { 5 }, 0, 1).GetAwaiter().GetResult();
            Check(ms.Length == 1, "MemoryStream WriteAsync");
        }
    }

    // RuntimeType compares ParentMethodTable against Enum / MulticastDelegate to classify types.
    private static void EnumAndDelegateShapes()
    {
        Check(typeof(Color).IsEnum, "IsEnum");
        Check(typeof(LongColor).IsEnum, "IsEnum long-backed");
        Check(!typeof(int).IsEnum, "int is not enum");
        Check(!typeof(Marked).IsEnum, "class is not enum");
        Check(Enum.GetUnderlyingType(typeof(Color)) == typeof(int), "enum underlying type");
        Check(Enum.GetUnderlyingType(typeof(LongColor)) == typeof(long), "enum underlying type long");
        Check(Enum.GetValues<Color>().Length == 3, "enum values");
        Check(Enum.IsDefined(typeof(Color), Color.Blue), "enum IsDefined");
        Check(Enum.Parse<Color>("Green") == Color.Green, "enum parse");
        Check(Color.Blue.ToString() == "Blue", "enum ToString");
        Check(((object)Color.Red).Equals(Color.Red), "boxed enum equality");

        Adder add = (x, y) => x + y;
        Check(add(2, 3) == 5, "delegate invoke");
        Check(typeof(Adder).IsSubclassOf(typeof(MulticastDelegate)), "delegate is MulticastDelegate");
        Check(typeof(Adder).BaseType == typeof(MulticastDelegate), "delegate base type");
        MethodInfo mi = typeof(Program).GetMethod("StaticAdd", BindingFlags.Static | BindingFlags.NonPublic);
        var made = (Adder)Delegate.CreateDelegate(typeof(Adder), mi);
        Check(made(4, 5) == 9, "created delegate invoke");
        Adder combined = (Adder)Delegate.Combine(add, made);
        Check(combined(1, 1) == 2, "combined delegate invoke");
    }

    private static int StaticAdd(int a, int b) => a + b;

    // Nullable<T> unbox data and the array element type handle live in the two union fields whose
    // offsets shift under compression (ElementTypeOffset / InterfaceMapOffset).
    private static void NullableAndArrayShapes()
    {
        int? n = 5;
        object boxed = n;
        Check(boxed is int, "boxed Nullable<int> is int");
        Check((int?)boxed == 5, "unbox to Nullable<int>");
        int? nothing = null;
        Check((object)nothing is null, "null Nullable boxes to null");
        long? big = 1L << 40;
        object boxedBig = big;
        Check((long?)boxedBig == (1L << 40), "unbox to Nullable<long>");
        Blittable? bn = new Blittable { A = 1, B = 2 };
        object boxedStruct = bn;
        Check(((Blittable?)boxedStruct).Value.B == 2, "unbox to Nullable<struct>");
        Check(Nullable.GetUnderlyingType(typeof(int?)) == typeof(int), "Nullable underlying type");

        Check(typeof(int[]).GetElementType() == typeof(int), "array element type int");
        Check(typeof(string[]).GetElementType() == typeof(string), "array element type string");
        Check(typeof(Marked[]).GetElementType() == typeof(Marked), "array element type class");
        Check(typeof(int[,]).GetArrayRank() == 2, "array rank");
        Array md = Array.CreateInstance(typeof(string), 2, 3);
        md.SetValue("v", 1, 2);
        Check((string)md.GetValue(1, 2) == "v", "multidimensional array round trip");
        Array created = Array.CreateInstance(typeof(Blittable), 4);
        Check(created.Length == 4 && created.GetType() == typeof(Blittable[]), "created struct array");

        // Array.Copy / Span over reference arrays revalidate element types against the mirror.
        var src = new string[] { "a", "b", "c" };
        var dst = new string[3];
        Array.Copy(src, dst, 3);
        Check(dst[2] == "c", "array copy");
        Check(RuntimeHelpers.GetHashCode(src) != 0, "GetHashCode on array");
        Span<string> span = src;
        Check(span[1] == "b", "span over reference array");
    }
}
