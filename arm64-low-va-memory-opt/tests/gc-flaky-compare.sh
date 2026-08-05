#!/bin/bash
# Repeats the GC tree on the device N times in one configuration and prints every failure name.
#
#   ./gc-flaky-compare.sh <on|off> [runs]
#
# Written because a single ON+shim run came back 103/2 while the runs either side of it came back
# 104/1 (GC/API/Frozen only) - same build, same shim. That is an intermittent failure, and telling
# "compression regression" from "device/timing flakiness" needs the same repetition in both
# configurations. Never truncate the failure list: the one run that showed the extra failure was
# piped through `head -3`, which lost the name.
#
# `on`  = the compressed build (both flags) + liblowvashim.so
# `off` = the default build, no shim
set -u
ROOT=/work/dotnet/new/runtime
M=/opt/usr/home/owner/media/coreclr
CFG=$1
RUNS=${2:-3}
T=$ROOT/arm64-low-va-memory-opt/tests

case "$CFG" in
  on)
    SRC=/home/clamp/Work/dotnet/on-build-backup
    for f in libcoreclr.so libclrgc.so libclrjit.so corerun; do
      sdb push "$SRC/$f" "$M/Core_Root/$f" >/dev/null 2>&1
    done
    sdb push "$SRC/System.Private.CoreLib.R2R.dll" "$M/Core_Root/System.Private.CoreLib.dll" >/dev/null 2>&1
    SHIM=--shim
    ;;
  off)
    B=$ROOT/artifacts/bin/coreclr/linux.arm64.Checked
    FX=$ROOT/artifacts/bin/testhost/net11.0-linux-Release-arm64/shared/Microsoft.NETCore.App/11.0.0
    for f in libcoreclr.so libclrgc.so libclrjit.so corerun; do
      sdb push "$B/$f" "$M/Core_Root/$f" >/dev/null 2>&1
    done
    sdb push "$FX/System.Private.CoreLib.dll" "$M/Core_Root/System.Private.CoreLib.dll" >/dev/null 2>&1
    SHIM=""
    ;;
  *) echo "usage: $0 <on|off> [runs]" >&2; exit 1 ;;
esac

sdb shell "chmod +x $M/Core_Root/corerun" >/dev/null 2>&1
echo "### config=$CFG runs=$RUNS"

for i in $(seq 1 "$RUNS"); do
  echo "--- run #$i"
  timeout 3000 "$T/run-device-coreclrtest.sh" GC/GC $SHIM 2>&1 | grep -E "^GC:|Failed test:"
done
