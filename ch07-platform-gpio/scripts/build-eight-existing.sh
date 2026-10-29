#!/usr/bin/env bash
# Offline only: app + disabled overlay + explicitly named active validation copy.
# No module build, boot config writes, overlay installation, or new toolchain.
set -euo pipefail
PROJECT=$(cd -- "$(dirname -- "$0")/.." && pwd)
YOCTO=${YOCTO:-$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose}
TMP=$YOCTO/build/tmp
KERNEL_SRC=$TMP/work-shared/raspberrypi4-64/kernel-source
KERNEL_BUILD=$TMP/work-shared/raspberrypi4-64/kernel-build-artifacts
NATIVE=$TMP/work/raspberrypi4_64-poky-linux/thermal-core/1.0/recipe-sysroot-native
APP_SYSROOT=$TMP/work/cortexa72-poky-linux/thermal-event-test/1.0/recipe-sysroot
CROSS=$NATIVE/usr/bin/aarch64-poky-linux/aarch64-poky-linux-
BASE_DTB=$TMP/deploy/images/raspberrypi4-64/bcm2711-rpi-4-b.dtb
for file in "${CROSS}gcc" "$APP_SYSROOT/usr/include/stdio.h" "$BASE_DTB" \
    "$KERNEL_BUILD/scripts/dtc/dtc" "$KERNEL_BUILD/scripts/dtc/fdtoverlay"; do
    test -f "$file" || { echo "Missing existing prerequisite: $file" >&2; exit 1; }
done
export PATH="$NATIVE/usr/bin:$NATIVE/bin:$NATIVE/usr/sbin:$NATIVE/usr/bin/aarch64-poky-linux:$PATH"
mkdir -p "$PROJECT/build"
"${CROSS}gcc" --sysroot="$APP_SYSROOT" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$PROJECT/include" "$PROJECT/userspace/lab_gpio_test.c" -o "$PROJECT/build/lab_gpio_test"
python3 - "$PROJECT" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = (p / 'dt/lab-gpio-eight-overlay.dts').read_text()
assert s.count('status = "disabled";') == 8
assert 'status = "okay";' not in s
(p / 'build/lab-gpio-eight-validation-active.dts').write_text(
    s.replace('status = "disabled";', 'status = "okay";'))
PY
for variant in disabled validation-active; do
    source=$PROJECT/dt/lab-gpio-eight-overlay.dts
    if [ "$variant" = validation-active ]; then
        source=$PROJECT/build/lab-gpio-eight-validation-active.dts
    fi
    output=$PROJECT/build/lab-gpio-eight-$variant
    "${CROSS}gcc" -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
        -I"$KERNEL_SRC/include" "$source" -o "$output.pp.dts"
    "$KERNEL_BUILD/scripts/dtc/dtc" -@ -I dts -O dtb -o "$output.dtbo" "$output.pp.dts"
    "$KERNEL_BUILD/scripts/dtc/fdtoverlay" -i "$BASE_DTB" \
        -o "$output.merged.dtb" "$output.dtbo"
done
"${CROSS}readelf" -h "$PROJECT/build/lab_gpio_test"
python3 "$PROJECT/tests/check_eight_offline.py"
echo 'OFFLINE ONLY: validation-active and merged DTBs are not installed or approved for target use.'
