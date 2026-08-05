// Exercises the code paths that actually read the MethodTable pointer out of an object header:
// casting, interface dispatch, virtual dispatch, generics over reference types, array covariance,
// reflection, boxing and exception type matching. Under FEATURE_COMPRESSED_MT that slot is 4 bytes
// wide, so a mistake in the narrowed read shows up here rather than only under allocation stress.
using System;
using System.Collections.Generic;
using System.Reflection;

internal interface IShape { double Area(); string Name { get; } }

internal abstract class Shape : IShape
{
    public abstract double Area();
    public virtual string Name => GetType().Name;
}

internal sealed class Circle : Shape
{
    public double R;
    public Circle(double r) { R = r; }
    public override double Area() => Math.PI * R * R;
}

internal sealed class Square : Shape
{
    public double S;
    public Square(double s) { S = s; }
    public override double Area() => S * S;
    public override string Name => "Sq:" + S.ToString("F0");
}

internal sealed class Box<T> where T : class
{
    public T Value;
    public Box(T v) { Value = v; }
    public Type Held() => typeof(T);
}

internal sealed class MyException : Exception
{
    public MyException(string m) : base(m) { }
}

internal static class Program
{
    private static int s_errors;

    private static void Check(bool cond, string what)
    {
        if (!cond) { s_errors++; Console.WriteLine("  FAIL: " + what); }
    }

    private static int Main()
    {
        Console.WriteLine("MethodTable-heavy stress (casting / dispatch / generics / reflection)");

        // --- virtual + interface dispatch, isinst/castclass ---
        var shapes = new List<IShape>();
        for (int i = 1; i <= 2000; i++)
        {
            shapes.Add((i % 2 == 0) ? (IShape)new Circle(i) : new Square(i));
        }

        int circles = 0, squares = 0;
        double total = 0;
        foreach (IShape s in shapes)
        {
            total += s.Area();                       // interface dispatch
            if (s is Circle c) { circles++; Check(c.R > 0, "circle cast"); }
            else if (s is Square q) { squares++; Check(q.S > 0, "square cast"); }
            Check(s.Name.Length > 0, "virtual property");
        }
        Check(circles == 1000 && squares == 1000, $"cast counts {circles}/{squares}");
        Check(total > 0, "area sum");

        // --- failed casts must throw, not corrupt ---
        object o = new Circle(1);
        try { var _ = (Square)o; Check(false, "invalid cast not thrown"); }
        catch (InvalidCastException) { }

        // --- array covariance store check (reads element MT) ---
        Shape[] arr = new Circle[16];
        try { arr[0] = new Square(1); Check(false, "array covariance not enforced"); }
        catch (ArrayTypeMismatchException) { }
        arr[0] = new Circle(2);
        Check(arr[0] is Circle, "covariant store");

        // --- generics over reference types ---
        var b1 = new Box<Circle>(new Circle(3));
        var b2 = new Box<Square>(new Square(4));
        Check(b1.Held() == typeof(Circle), "generic type handle 1");
        Check(b2.Held() == typeof(Square), "generic type handle 2");

        var dict = new Dictionary<string, IShape>();
        for (int i = 0; i < 500; i++) dict["k" + i] = new Circle(i + 1);
        Check(dict.Count == 500, "dictionary count");
        Check(dict["k10"] is Circle, "dictionary value type");

        // --- boxing / unboxing ---
        for (int i = 0; i < 2000; i++)
        {
            object boxed = i;
            Check((int)boxed == i, "unbox");
            object bd = (double)i;
            Check(bd is double, "boxed double type");
        }

        // --- GetType / reflection (heavy MethodTable use) ---
        foreach (IShape s in shapes.GetRange(0, 200))
        {
            Type t = s.GetType();
            Check(t == typeof(Circle) || t == typeof(Square), "GetType");
            MethodInfo mi = t.GetMethod("Area");
            Check(mi != null, "GetMethod Area");
            double a = (double)mi.Invoke(s, null);
            Check(Math.Abs(a - s.Area()) < 1e-9, "reflective invoke matches");
        }

        // --- exception type matching walks the MT parent chain ---
        int caught = 0;
        for (int i = 0; i < 500; i++)
        {
            try
            {
                if ((i % 3) == 0) throw new MyException("m" + i);
                if ((i % 3) == 1) throw new InvalidOperationException("i" + i);
                throw new ArgumentException("a" + i);
            }
            catch (MyException) { caught++; }
            catch (InvalidOperationException) { caught++; }
            catch (Exception) { caught++; }
        }
        Check(caught == 500, $"exception matching {caught}");

        // --- force GCs so objects move while their MT slots are read again ---
        GC.Collect(2, GCCollectionMode.Forced, true);
        GC.WaitForPendingFinalizers();
        GC.Collect(2, GCCollectionMode.Forced, true);

        foreach (IShape s in shapes)
        {
            Check(s.Area() > 0, "area after GC");
            Check(s.GetType() != null, "GetType after GC");
        }

        Console.WriteLine($"errors = {s_errors}");
        Console.WriteLine(s_errors == 0 ? "RESULT: PASS" : "RESULT: FAIL");
        return s_errors == 0 ? 100 : 1;
    }
}
