#!/bin/bash
# Runs a CoreCLR runtime-test merged assembly (src/tests) under the standard low-VA QEMU
# environment, and prints the pass/fail tally.
#
#   ./run-lowva-coreclrtest.sh <merged-test-dir-or-dll> [NAME=value ...]
#
# e.g. ./run-lowva-coreclrtest.sh \
#        artifacts/tests/coreclr/linux.arm64.Checked/JIT/CodeGenBringUpTests/JIT.CodeGenBringUpTests_d
#
# Differences from run-lowva.sh:
#   * CORE_ROOT is the runtime test layout, not the testhost, because the tests need the xunit
#     assemblies that live there.
#   * The working directory must be the merged test directory: the merged runner loads the
#     individual test assemblies from beside itself.
#
# Individual test assemblies are NOT directly runnable - they are xunit [Fact] libraries with no
# entry point ("Entry point not found in assembly"). Always run the JIT.<Area>_{d,do,r,ro} merged
# assembly, which is the one with a generated Main.
#
# Prerequisites (see ../STAGE-B-POINTER-COMPRESSION.md, "CoreCLR 테스트 스위트"):
#   ROOTFS_DIR=... ./src/tests/build.sh -arch arm64 -checked -cross -generatelayoutonly \
#       /p:IsXUnitLogCheckerSupported=false
#   ROOTFS_DIR=... ./src/tests/build.sh -arch arm64 -checked -cross -priority1 -skipnative \
#       -skipgeneratelayout -tree JIT/CodeGenBringUpTests /p:IsXUnitLogCheckerSupported=false
set -u
ROOT=/work/dotnet/new/runtime
R=/home/clamp/Work/dotnet/rootfs/arm64.tizen
CR=$ROOT/artifacts/tests/coreclr/linux.arm64.Checked/Tests/Core_Root

TARGET=$1; shift
case "$TARGET" in /*) ;; *) TARGET=$ROOT/$TARGET ;; esac
if [ -d "$TARGET" ]; then
  DIR=$TARGET
  DLL=$(basename "$TARGET").dll
else
  DIR=$(dirname "$TARGET")
  DLL=$(basename "$TARGET")
fi

LOG=$(mktemp)
cd "$DIR" || exit 1
# CORE_ROOT must be exported, not just passed to corerun with -c: the out-of-process tests (Loader
# spawns child processes through a generated .sh) invoke "$CORE_ROOT/corerun" themselves, and
# without it they fail with "/corerun: No such file or directory".
timeout 1800 env CORE_ROOT="$CR" DOTNET_GCRegionRange=40000000 "$@" \
  qemu-aarch64 -R 0x100000000 -L "$R" "$R/lib64/ld-linux-aarch64.so.1" \
    --library-path "$R/lib64:$R/usr/lib64:$CR" "$CR/corerun" -c "$CR" "$DLL" > "$LOG" 2>&1
RC=$?

PASS=$(grep -c "Passed test:" "$LOG")
FAIL=$(grep -c "Failed test:" "$LOG")
echo "$DLL: passed=$PASS failed=$FAIL exit=$RC"
if [ "$FAIL" -gt 0 ]; then
  echo "--- failures ---"
  grep -A 3 "Failed test:" "$LOG" | head -60
fi
if [ "$RC" -ne 100 ] && [ "$RC" -ne 0 ]; then
  echo "--- tail of output (exit $RC) ---"
  tail -20 "$LOG"
fi
rm -f "$LOG"
