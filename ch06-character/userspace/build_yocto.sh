#!/usr/bin/env bash
# Compile the 06B userspace test using the existing Yocto target toolchain.
# Does not modify or rebuild the thermal_core driver.
set -eo pipefail

PROJECT="$HOME/study/linux-system-programming/ch06-character"
YOCTO="$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose"
RECIPE="$YOCTO/layers/meta-yocto-lab/recipes-kernel/thermal-test"
SHARE="/mnt/c/Users/Public/arm64-share"

for path in "$PROJECT/userspace/thermal_test.c" \
            "$PROJECT/yocto-recipe/thermal-test_1.0.bb" \
            "$YOCTO/layers/openembedded-core/oe-init-build-env" \
            "$SHARE"; do
    if [ ! -e "$path" ]; then
        echo "[ERROR] Required path missing: $path" >&2
        exit 1
    fi
done

mkdir -p "$RECIPE/files"
cp "$PROJECT/yocto-recipe/thermal-test_1.0.bb" "$RECIPE/"
cp "$PROJECT/userspace/thermal_test.c" "$RECIPE/files/"

source "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"
bitbake thermal-test

B=$(bitbake -e thermal-test | sed -n 's/^B="\(.*\)"/\1/p')
if [ -z "$B" ] || [ ! -f "$B/thermal_test" ]; then
    echo "[ERROR] Could not find built thermal_test in B=$B" >&2
    exit 1
fi
if ! readelf -h "$B/thermal_test" | grep -q 'Machine:.*AArch64'; then
    echo "[ERROR] Built program is not AArch64: $B/thermal_test" >&2
    exit 1
fi
cp "$B/thermal_test" "$SHARE/thermal_test"
echo "[PASS] Yocto ARM64 test app staged: $SHARE/thermal_test"
file "$SHARE/thermal_test"
