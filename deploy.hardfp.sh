#!/usr/bin/env bash
# Build the armel CoreCLR (Checked) and deploy it to the connected device's Core_Root, then push the
# HardFPTest harness. Modeled on auto.arm.chk.sh.
#
# Usage:
#   ./deploy.hardfp.sh            # rebuild clr, package + sdb push runtime + test
#   ./deploy.hardfp.sh --full     # rebuild clr+libs (needed the first time / after libs changes)
#   ./deploy.hardfp.sh --no-build  # skip building, just package + push existing artifacts
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ROOTFS_DIR=/home/clamp/Work.bak/dotnet/rootfs/armel

# Where the runtime lives on the device (from auto.arm.chk.sh). Adjust if your device differs.
CORE_ROOT_ON_DEVICE=/home/owner/media/Work/idiv/test/Tests/Core_Root

SUBSET="clr"
DO_BUILD=1
for arg in "$@"; do
  case "$arg" in
    --full)     SUBSET="clr+libs" ;;
    --no-build) DO_BUILD=0 ;;
    *) echo "unknown arg: $arg"; exit 2 ;;
  esac
done

cd "$REPO"

if [[ "$DO_BUILD" == "1" ]]; then
  echo ">>> Building $SUBSET (armel, Checked runtime / Release libs)"
  ./build.sh --cross --clang --arch armel -rc Checked -lc Release -hc Checked \
    --subset "$SUBSET" --cmakeargs -DFEATURE_IBCLOGGER=true
fi

# --- package + push the runtime (same layout as auto.arm.chk.sh) ---
echo ">>> Packaging + pushing CoreCLR"
cd "$REPO/artifacts/bin/coreclr"
tar czf clr.tar.gz -C linux.armel.Checked .
sdb push clr.tar.gz "$CORE_ROOT_ON_DEVICE"

echo ">>> Packaging + pushing libraries runtime"
cd "$REPO/artifacts/bin/runtime"
tar czf runtime.tar.gz -C net11.0-linux-Release-armel .
sdb push runtime.tar.gz "$CORE_ROOT_ON_DEVICE"

# --- unpack on the device ---
echo ">>> Unpacking on device"
sdb shell "cd $CORE_ROOT_ON_DEVICE && tar xzf clr.tar.gz && tar xzf runtime.tar.gz && rm -f clr.tar.gz runtime.tar.gz"

# --- build + push the test harness (managed IL is architecture independent) ---
echo ">>> Building HardFPTest (host) and pushing"
"$REPO/dotnet.sh" build "$REPO/hardfp-test/HardFPTest.csproj" -c Release -o "$REPO/hardfp-test/bin"
sdb push "$REPO/hardfp-test/bin/HardFPTest.dll" "$CORE_ROOT_ON_DEVICE"

cat <<EOF

>>> Done. On the device, run baseline vs. experiment and diff the results:

  sdb shell "cd $CORE_ROOT_ON_DEVICE && ./corerun HardFPTest.dll"
  sdb shell "cd $CORE_ROOT_ON_DEVICE && DOTNET_JitManagedHardFP=1 ./corerun HardFPTest.dll"

Both must print 'ALL TESTS PASSED'. To inspect codegen (verify vmov removal on managed calls):

  sdb shell "cd $CORE_ROOT_ON_DEVICE && DOTNET_JitManagedHardFP=1 DOTNET_JitDisasm=AddF ./corerun HardFPTest.dll"
EOF
