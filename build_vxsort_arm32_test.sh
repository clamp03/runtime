#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

SYSROOT=/home/clamp/Work.bak/dotnet/rootfs/armel
GCCTC=$SYSROOT/usr
OBJDIR=artifacts/obj/coreclr/linux.armel.Checked/gc/vxsort/CMakeFiles/gc_vxsort.dir
OUT=vxsort_arm32_test
mkdir -p "$OUT"

CXX=/usr/bin/clang++-21

FLAGS=(
  --target=arm-linux-gnueabi
  --gcc-toolchain="$GCCTC"
  --sysroot="$SYSROOT"
  -std=gnu++17 -fPIC -mthumb -mfpu=neon-vfpv3 -mfloat-abi=softfp -march=armv7-a
  -isystem "$SYSROOT/usr/lib/gcc/armv7l-tizen-linux-gnueabi/9.2.0/include/c++"
  -isystem "$SYSROOT/usr/lib/gcc/armv7l-tizen-linux-gnueabi/9.2.0/include/c++/armv7l-tizen-linux-gnueabi"
)

DEFINES=(-DTARGET_ARM -DTARGET_32BIT -DTARGET_UNIX -DTARGET_LINUX -DCPU_FEATURES_ARCH_ARM -DCLR_CMAKE_HOST_UNIX)
INCLUDES=(-Isrc/coreclr/gc/vxsort -Isrc/coreclr/gc/env -Isrc/coreclr/gc -Isrc/native -Isrc/native/inc -Isrc/coreclr/pal/prebuilt/inc -Iartifacts/obj)

echo "=== [1/2] demo.cpp 컴파일 (asserts 활성: NDEBUG 미정의) ==="
"$CXX" "${FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" -O0 -g \
  -c src/coreclr/gc/vxsort/standalone/simple_bench/demo.cpp -o "$OUT/demo.o"
echo "  OK"

GCCLIB=$SYSROOT/usr/lib/gcc/armv7l-tizen-linux-gnueabi/9.2.0
LINKFLAGS=(
  -B"$GCCLIB"
  -L"$SYSROOT/lib" -L"$SYSROOT/usr/lib" -L"$GCCLIB"
  -Wl,--rpath-link="$SYSROOT/lib" -Wl,--rpath-link="$SYSROOT/usr/lib"
)

echo "=== [2/2] 기존 vxsort .o들과 링크 (libstdc++ 정적) ==="
"$CXX" "${FLAGS[@]}" "${LINKFLAGS[@]}" -static-libstdc++ -static-libgcc \
  "$OUT/demo.o" \
  "$OBJDIR/do_vxsort_neon.cpp.o" \
  "$OBJDIR/isa_detection.cpp.o" \
  "$OBJDIR/machine_traits.neon.cpp.o" \
  "$OBJDIR/smallsort/bitonic_sort.NEON.uint32_t.generated.cpp.o" \
  "$OBJDIR/smallsort/bitonic_sort.scalar.uint64_t.generated.cpp.o" \
  -lm -o "$OUT/vxsort_arm32_test"
echo "  OK -> $OUT/vxsort_arm32_test"
file "$OUT/vxsort_arm32_test"
