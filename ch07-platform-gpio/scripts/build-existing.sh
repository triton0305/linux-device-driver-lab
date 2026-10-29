#!/usr/bin/env bash
# Reuse existing Yocto toolchain/sysroots/kernel artifacts; never bootstrap a tree.
set -euo pipefail
PROJECT=$(cd -- "$(dirname -- "$0")/.." && pwd)
YOCTO=${YOCTO:-$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose}
TMP=$YOCTO/build/tmp
KERNEL_SRC=$TMP/work-shared/raspberrypi4-64/kernel-source
KERNEL_BUILD=$TMP/work-shared/raspberrypi4-64/kernel-build-artifacts
NATIVE=$TMP/work/raspberrypi4_64-poky-linux/thermal-core/1.0/recipe-sysroot-native
APP_SYSROOT=$TMP/work/cortexa72-poky-linux/thermal-event-test/1.0/recipe-sysroot
CROSS=$NATIVE/usr/bin/aarch64-poky-linux/aarch64-poky-linux-
for file in "$KERNEL_BUILD/Module.symvers" "$KERNEL_BUILD/.config" "${CROSS}gcc" \
    "$APP_SYSROOT/usr/include/stdio.h" "$KERNEL_BUILD/scripts/dtc/dtc"; do
    test -f "$file" || { echo "Missing existing build prerequisite: $file" >&2; exit 1; }
done
export PATH="$NATIVE/usr/bin:$NATIVE/bin:$NATIVE/usr/sbin:$NATIVE/usr/bin/aarch64-poky-linux:$PATH"
mkdir -p "$PROJECT/build"
make -C "$KERNEL_SRC" O="$KERNEL_BUILD" M="$PROJECT/driver" \
    ARCH=arm64 CROSS_COMPILE="$CROSS" W=1 modules
"${CROSS}gcc" --sysroot="$APP_SYSROOT" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$PROJECT/include" "$PROJECT/userspace/lab_gpio_test.c" -o "$PROJECT/build/lab_gpio_test"
"${CROSS}gcc" -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
    -I"$KERNEL_SRC/include" "$PROJECT/dt/lab-gpio-overlay.dts" \
    -o "$PROJECT/build/lab-gpio-overlay.pp.dts"
"$KERNEL_BUILD/scripts/dtc/dtc" -@ -I dts -O dtb \
    -o "$PROJECT/build/lab-gpio.dtbo" "$PROJECT/build/lab-gpio-overlay.pp.dts"
# Offline file merge only. This is NOT installation on a target.
"$KERNEL_BUILD/scripts/dtc/fdtoverlay" \
    -i "$TMP/deploy/images/raspberrypi4-64/bcm2711-rpi-4-b.dtb" \
    -o "$PROJECT/build/merged-offline.dtb" "$PROJECT/build/lab-gpio.dtbo"
"${CROSS}readelf" -h "$PROJECT/driver/lab_gpio.ko" "$PROJECT/build/lab_gpio_test"
