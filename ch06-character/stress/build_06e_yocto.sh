#!/usr/bin/env bash
set -eo pipefail

PROJECT="$HOME/study/linux-system-programming/ch06-character"
YOCTO="$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose"
RECIPE="$YOCTO/layers/meta-yocto-lab/recipes-test/thermal-stress-test"
SHARE="/mnt/c/Users/Public/arm64-share"

mkdir -p "$RECIPE/files"

cp "$PROJECT/yocto-recipe/thermal-stress-test_1.0.bb" "$RECIPE/"
cp "$PROJECT/stress/thermal_stress_test.c" "$RECIPE/files/"
cp "$PROJECT/include/thermal_uapi.h" "$RECIPE/files/"

source "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"

bitbake thermal-stress-test

B=$(bitbake -e thermal-stress-test | sed -n 's/^B="\(.*\)"/\1/p')

if [ -z "$B" ] || [ ! -s "$B/thermal_stress_test" ]; then
    echo "[ERROR] ARM64 executable not found" >&2
    exit 1
fi

readelf -h "$B/thermal_stress_test" | grep -q 'Machine:.*AArch64'

mkdir -p "$SHARE"
cp "$B/thermal_stress_test" "$SHARE/thermal_stress_test"

echo "[PASS] 06E ARM64 executable staged"
file "$SHARE/thermal_stress_test"
