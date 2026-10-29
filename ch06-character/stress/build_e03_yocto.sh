#!/usr/bin/env bash
set -eo pipefail
PROJECT="$HOME/study/linux-system-programming/ch06-character"
YOCTO="$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose"
RECIPE="$YOCTO/layers/meta-yocto-lab/recipes-test/thermal-multi-consumer-test"
SHARE="/mnt/c/Users/Public/arm64-share"
for path in "$PROJECT/stress/thermal_multi_consumer_test.c" "$PROJECT/yocto-recipe/thermal-multi-consumer-test_1.0.bb" "$PROJECT/include/thermal_uapi.h" "$YOCTO/layers/openembedded-core/oe-init-build-env" "$SHARE"; do
    if [ ! -e "$path" ]; then echo "[ERROR] Missing: $path" >&2; exit 1; fi
done
mkdir -p "$RECIPE/files"
cp "$PROJECT/yocto-recipe/thermal-multi-consumer-test_1.0.bb" "$RECIPE/"
cp "$PROJECT/stress/thermal_multi_consumer_test.c" "$RECIPE/files/"
cp "$PROJECT/include/thermal_uapi.h" "$RECIPE/files/"
set +u
source "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"
bitbake thermal-multi-consumer-test
B=$(bitbake -e thermal-multi-consumer-test | sed -n 's/^B="\(.*\)"/\1/p')
if [ -z "$B" ] || [ ! -s "$B/thermal_multi_consumer_test" ]; then
    echo "[ERROR] Could not find executable in B=$B" >&2; exit 1
fi
if ! readelf -h "$B/thermal_multi_consumer_test" | grep -q 'Machine:.*AArch64'; then
    echo "[ERROR] Not AArch64" >&2; exit 1
fi
cp "$B/thermal_multi_consumer_test" "$SHARE/thermal_multi_consumer_test"
echo "[PASS] E03 ARM64 executable staged: $SHARE/thermal_multi_consumer_test"
file "$SHARE/thermal_multi_consumer_test"
