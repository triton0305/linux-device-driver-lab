#!/usr/bin/env bash
set -eo pipefail
PROJECT="$HOME/study/linux-system-programming/ch06-character"
YOCTO="$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose"
RECIPE="$YOCTO/layers/meta-yocto-lab/recipes-test/thermal-multi-producer-test"
SHARE="/mnt/c/Users/Public/arm64-share"
for path in "$PROJECT/stress/thermal_multi_producer_test.c" "$PROJECT/yocto-recipe/thermal-multi-producer-test_1.0.bb" "$PROJECT/include/thermal_uapi.h" "$YOCTO/layers/openembedded-core/oe-init-build-env"; do
    if [ ! -f "$path" ]; then echo "[ERROR] Missing: $path" >&2; exit 1; fi
done
mkdir -p "$RECIPE/files" "$SHARE"
cp "$PROJECT/yocto-recipe/thermal-multi-producer-test_1.0.bb" "$RECIPE/"
cp "$PROJECT/stress/thermal_multi_producer_test.c" "$RECIPE/files/"
cp "$PROJECT/include/thermal_uapi.h" "$RECIPE/files/"
set +u
source "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"
bitbake thermal-multi-producer-test
B=$(bitbake -e thermal-multi-producer-test | sed -n 's/^B="\(.*\)"/\1/p')
if [ -z "$B" ] || [ ! -s "$B/thermal_multi_producer_test" ]; then
    echo "[ERROR] AArch64 executable not found: B=$B" >&2
    exit 1
fi
if ! readelf -h "$B/thermal_multi_producer_test" | grep 'Machine:.*AArch64' >/dev/null; then
    echo "[ERROR] Not AArch64" >&2
    exit 1
fi
cp "$B/thermal_multi_producer_test" "$SHARE/thermal_multi_producer_test"
echo "[PASS] E02 ARM64 executable staged: $SHARE/thermal_multi_producer_test"
file "$SHARE/thermal_multi_producer_test"
