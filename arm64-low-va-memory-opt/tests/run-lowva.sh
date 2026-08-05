#!/bin/bash
# Runs a test app in the standard low-VA environment used throughout this project.
#
#   ./run-lowva.sh <app.dll> [NAME=value ...] [app args ...]
#
# Arguments that look like NAME=value are passed to the runtime as environment variables
# (e.g. DOTNET_CompressedPtrHeapCensus=1); everything else is forwarded to the app.
#
# Both `qemu -R 0x100000000` (confines the whole guest address space to 4 GB) and
# DOTNET_GCRegionRange are required: without the latter the GC's default ~61 GiB reservation does
# not fit under the 4 GB ceiling and startup fails with 0x8007000E.
#
# Build a test app first with:  /work/dotnet/new/runtime/dotnet.sh build -c Release
# See ../STAGE-B-POINTER-COMPRESSION.md.
set -u
cd /work/dotnet/new/runtime
R=/home/clamp/Work/dotnet/rootfs/arm64.tizen
CR=$(pwd)/artifacts/bin/testhost/net11.0-linux-Release-arm64/shared/Microsoft.NETCore.App/11.0.0
APP=$1; shift

ENVS=()
ARGS=()
for a in "$@"; do
  if [[ $a == [A-Za-z_]*=* ]]; then ENVS+=("$a"); else ARGS+=("$a"); fi
done

timeout 900 env DOTNET_GCRegionRange=40000000 "${ENVS[@]}" \
  qemu-aarch64 -R 0x100000000 -L $R $R/lib64/ld-linux-aarch64.so.1 \
    --library-path $R/lib64:$R/usr/lib64:$CR $CR/corerun -c $CR "$APP" "${ARGS[@]}"
echo "EXIT=$?"
