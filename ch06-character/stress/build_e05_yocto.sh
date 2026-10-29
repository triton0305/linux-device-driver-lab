#!/usr/bin/env bash
set -eo pipefail
PROJECT="$HOME/study/linux-system-programming/ch06-character"
YOCTO="$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose"
RECIPE="$YOCTO/layers/meta-yocto-lab/recipes-test/thermal-latency-test"
SHARE="/mnt/c/Users/Public/arm64-share"
for path in "$PROJECT/stress/thermal_latency_test.c" "$PROJECT/yocto-recipe/thermal-latency-test_1.0.bb" "$PROJECT/include/thermal_uapi.h" "$YOCTO/layers/openembedded-core/oe-init-build-env" "$SHARE"; do
    if [ ! -e "$path" ]; then echo "[ERROR] Missing: $path" >&2; exit 1; fi
done
mkdir -p "$RECIPE/files"
cp "$PROJECT/yocto-recipe/thermal-latency-test_1.0.bb" "$RECIPE/"
cp "$PROJECT/stress/thermal_latency_test.c" "$RECIPE/files/"
cp "$PROJECT/include/thermal_uapi.h" "$RECIPE/files/"
set +u
source "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"
bitbake thermal-latency-test
B=$(bitbake -e thermal-latency-test | sed -n 's/^B="\(.*\)"/\1/p')
if [ -z "$B" ] || [ ! -s "$B/thermal_latency_test" ]; then
    echo "[ERROR] Could not find thermal_latency_test in B=$B" >&2; exit 1
fi
if ! readelf -h "$B/thermal_latency_test" | grep -q 'Machine:.*AArch64'; then
    echo "[ERROR] Not AArch64: $B/thermal_latency_test" >&2; exit 1
fi
cp "$B/thermal_latency_test" "$SHARE/thermal_latency_test"
echo "[PASS] E05 ARM64 executable staged: $SHARE/thermal_latency_test"
file "$SHARE/thermal_latency_test"
