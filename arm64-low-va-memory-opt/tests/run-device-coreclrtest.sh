#!/bin/bash
# Runs a CoreCLR runtime-test merged assembly on the physical Tizen device.
# Device counterpart of run-lowva-coreclrtest.sh (which uses QEMU).
#
#   ./run-device-coreclrtest.sh <path-under-tests> [--shim] [NAME=value ...]
#
# e.g. ./run-device-coreclrtest.sh JIT.CodeGenBringUpTests_d --shim
#      ./run-device-coreclrtest.sh Loader/Loader --shim      # nested: <tree>/<merged dir>
#      ./run-device-coreclrtest.sh GC/GC --shim
#
# The assembly name is the basename of the path, matching how the test build lays merged runners out
# (JIT/CodeGenBringUpTests/JIT.CodeGenBringUpTests_d/, Loader/Loader/, GC/GC/).
#
# --shim  preloads liblowvashim.so, which steers the runtime's reservations below 4 GB. Required for
#         a compressed-pointer build: without it the CompressedPtr assert trips at startup, because
#         the kernel places every mapping above 4 GB on this board (see tests/vaprobe).
#
# Layout expected on the device (push with the commands in the project HANDOVER):
#   /opt/usr/home/owner/media/coreclr/Core_Root                 framework + corerun + xunit
#   /opt/usr/home/owner/media/coreclr/tests/<merged-name>/       merged runner + its test assemblies
#
# The media partition is used because it has ~55 GB free, unlike / (1.2 GB) and /opt (1.3 GB).
# The working directory must be the merged directory: the runner loads the individual test
# assemblies from beside itself.
#
# Exit code 100 = pass (CoreCLR test suite convention).
set -u
M=/opt/usr/home/owner/media/coreclr
SHIM=/opt/vatest/liblowvashim.so

NAME=$1; shift
DLL=$(basename "$NAME")
PRELOAD=""
ENVS=""
for a in "$@"; do
  case "$a" in
    --shim) PRELOAD="LD_PRELOAD=$SHIM" ;;
    [A-Za-z_]*=*) ENVS="$ENVS $a" ;;
  esac
done

LOG=$(mktemp)
sdb shell "cd $M/tests/$NAME && export CORE_ROOT=$M/Core_Root && $PRELOAD DOTNET_GCRegionRange=40000000$ENVS \
  $M/Core_Root/corerun -c $M/Core_Root ./$DLL.dll; echo DEVICE_EXIT=\$?" > "$LOG" 2>&1
tr -d '\r' < "$LOG" > "$LOG.clean"; mv "$LOG.clean" "$LOG"

PASS=$(grep -c "Passed test:" "$LOG")
FAIL=$(grep -c "Failed test:" "$LOG")
RC=$(grep -o 'DEVICE_EXIT=[0-9]*' "$LOG" | tail -1 | cut -d= -f2)
echo "$DLL: passed=$PASS failed=$FAIL exit=${RC:-?}"

if [ "$FAIL" -gt 0 ]; then
  echo "--- failures ---"; grep -A3 "Failed test:" "$LOG" | head -60
fi
if [ "${RC:-1}" != "100" ]; then
  echo "--- tail (exit ${RC:-?}) ---"; tail -20 "$LOG"
fi
rm -f "$LOG"
