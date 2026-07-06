// TEST-ONLY: ARM32 vxsort GC-integration stress.
// GC mark-list sort (do_vxsort -> do_vxsort_neon) only fires when, per ephemeral GC:
//   * condemned generation is ephemeral (gen0/gen1), and
//   * the mark list did NOT overflow (survivors < mark_list cap, ~8192 here), and
//   * (server GC) total survivors <= total_ephemeral_size / 256  (mark_phase.cpp).
// So: keep a SMALL surviving set (3000) while churning lots of short-lived garbage.
// Validity is checked by the Checked build's _DEBUG sortedness assert (gc.cpp ~5814);
// if the ARM32 sort were wrong the process would abort instead of printing DONE.
using System;
class P {
    static object[] root = new object[3000];
    static void Main() {
        for (int iter = 0; iter < 400; iter++) {
            for (int i = 0; i < root.Length; i++) root[i] = new byte[8];          // few survivors (ephemeral)
            for (int j = 0; j < 100000; j++) { var t = new byte[16]; GC.KeepAlive(t); } // lots of garbage
            GC.Collect(0);                                                         // ephemeral GC -> mark-list sort
        }
        Console.WriteLine("GC vxsort integration: DONE ok");
    }
}
