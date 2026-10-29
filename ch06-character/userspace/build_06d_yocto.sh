#!/usr/bin/env bash
set -eo pipefail
PROJECT="$HOME/study/linux-system-programming/ch06-character"
YOCTO="$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose"
RECIPE="$YOCTO/layers/meta-yocto-lab/recipes-test/thermal-event-test"
SHARE="/mnt/c/Users/Public/arm64-share"
for path in "$PROJECT/userspace/thermal_event_test.c" "$PROJECT/yocto-recipe/thermal-event-test_1.0.bb" "$PROJECT/include/thermal_uapi.h" "$YOCTO/layers/openembedded-core/oe-init-build-env" "$SHARE"; do
    if [ ! -e "$path" ]; then echo "[ERROR] Missing: $path" >&2; exit 1; fi
done
mkdir -p "$RECIPE/files"
cp "$PROJECT/yocto-recipe/thermal-event-test_1.0.bb" "$RECIPE/"
cp "$PROJECT/userspace/thermal_event_test.c" "$RECIPE/files/"
cp "$PROJECT/include/thermal_uapi.h" "$RECIPE/files/"
set +u
source "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"
bitbake thermal-event-test
B=$(bitbake -e thermal-event-test | sed -n 's/^B="\(.*\)"/\1/p')
if [ -z "$B" ] || [ ! -s "$B/thermal_event_test" ]; then
    echo "[ERROR] Could not find thermal_event_test in B=$B" >&2; exit 1
fi
if ! readelf -h "$B/thermal_event_test" | grep -q 'Machine:.*AArch64'; then
    echo "[ERROR] Not AArch64: $B/thermal_event_test" >&2; exit 1
fi
cp "$B/thermal_event_test" "$SHARE/thermal_event_test"
echo "[PASS] Yocto ARM64 06D test staged: $SHARE/thermal_event_test"
file "$SHARE/thermal_event_test"
