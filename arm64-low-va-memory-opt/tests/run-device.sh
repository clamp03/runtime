#!/bin/bash
# Runs one test app on the physical Tizen device (counterpart of run-lowva.sh, which uses QEMU).
#
#   ./run-device.sh <app-name> [NAME=value ...] [app args ...]
#
# Differences from the QEMU path that matter:
#
#   * There is no `qemu -R 0x100000000`, so the low-VA invariant is NOT enforced. Measured on this
#     board (see vaprobe): a plain mmap lands at ~0xffff80000000 and every startup mapping is above
#     4 GB. A build with compressed pointers enabled is therefore expected to trip the
#     CompressedPtr assert here until the runtime places its own reservations low (Phase A1a/A1b).
#   * DOTNET_GCRegionRange is still passed: it keeps the GC's default ~61 GiB reservation down to
#     something this 2 GB board can actually reserve.
#
# Deploy first with ./deploy-device.sh.
set -u
DEV=/opt/vatest
APP=$1; shift

ENVS=""
ARGS=""
for a in "$@"; do
  if [[ $a == [A-Za-z_]*=* ]]; then ENVS="$ENVS $a"; else ARGS="$ARGS $a"; fi
done

sdb shell "cd $DEV && DOTNET_GCRegionRange=40000000$ENVS \
  $DEV/fx/corerun -c $DEV/fx $DEV/apps/$APP.dll$ARGS; echo EXIT=\$?" 2>&1 | tr -d '\r'
