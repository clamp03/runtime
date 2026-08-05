#!/bin/bash
# Pushes the built arm64 runtime plus the project's test apps to the physical Tizen device.
#
#   ./deploy-device.sh [--runtime-only]
#
# Layout on the device mirrors the host testhost framework directory, so `corerun -c <dir> app.dll`
# works the same way it does under QEMU:
#
#   /opt/vatest/fx/    framework assemblies + libcoreclr.so / libclrjit.so / libclrgc.so / corerun
#   /opt/vatest/apps/  the test app dlls
#
# /opt is used rather than /tmp because /tmp is a 1.9 GB tmpfs (RAM) on this board.
#
# The device has no `qemu -R`, so the 4 GB invariant does NOT hold here - see run-device.sh.
set -u
ROOT=/work/dotnet/new/runtime
FX=$ROOT/artifacts/bin/testhost/net11.0-linux-Release-arm64/shared/Microsoft.NETCore.App/11.0.0
B=$ROOT/artifacts/bin/coreclr/linux.arm64.Checked
T=$ROOT/arm64-low-va-memory-opt/tests
DEV=/opt/vatest

if [ ! -d "$FX" ]; then echo "missing testhost framework: $FX" >&2; exit 1; fi

# Keep the framework copy in sync with the runtime that was just built.
for f in libcoreclr.so libclrgc.so libclrjit.so corerun; do cp -f "$B/$f" "$FX/$f"; done

sdb root on >/dev/null 2>&1
sdb shell "mkdir -p $DEV/fx $DEV/apps" >/dev/null 2>&1

if [ "${1:-}" != "--runtime-only" ]; then
  echo "== pushing framework (this is the slow part, ~250 MB)"
  sdb push "$FX" "$DEV/fx" 2>&1 | tail -1
else
  echo "== pushing runtime binaries only"
  for f in libcoreclr.so libclrgc.so libclrjit.so corerun System.Private.CoreLib.dll; do
    sdb push "$FX/$f" "$DEV/fx/$f" 2>&1 | tail -1
  done
fi

echo "== pushing test apps"
for a in mtstress mirrorstress lowvatest lowvastress sizemeasure heapcensus refload; do
  dll=$T/$a/bin/Release/net11.0/$a.dll
  [ -f "$dll" ] && sdb push "$dll" "$DEV/apps/$a.dll" >/dev/null 2>&1 && echo "   $a"
done

sdb shell "chmod +x $DEV/fx/corerun; ls $DEV/fx/corerun; du -sh $DEV" 2>&1 | tr -d '\r'
