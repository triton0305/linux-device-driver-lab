#!/usr/bin/env bash
# 06C: patch/stage known working driver, build AArch64 kernel module + test app.
set -euo pipefail

PROJECT="$HOME/study/linux-system-programming/ch06-character"
KIT="$PROJECT/ch06c-kit"
YOCTO="$HOME/study/rootfs/yocto/bitbake-builds/poky-wrynose"
SHARE="/mnt/c/Users/Public/arm64-share"

for path in "$KIT/prepare.py" "$YOCTO/layers/openembedded-core/oe-init-build-env" "$SHARE"; do
    if [ ! -e "$path" ]; then
        echo "[ERROR] Missing: $path" >&2
        exit 1
    fi
done

python3 "$KIT/prepare.py"
set +u
source "$YOCTO/layers/openembedded-core/oe-init-build-env" "$YOCTO/build"
set -u
bitbake thermal-core thermal-ioctl-test

value_of() {
    local recipe="$1" variable="$2"
    bitbake -e "$recipe" | sed -n "s/^${variable}=\"\(.*\)\"/\1/p" | tail -n 1
}

CORESRC="$(value_of thermal-core S)"
COREBUILD="$(value_of thermal-core B)"
COREWORK="$(value_of thermal-core WORKDIR)"
APPBUILD="$(value_of thermal-ioctl-test B)"

KO=''
for candidate in "$CORESRC/thermal_core.ko" "$COREBUILD/thermal_core.ko"; do
    if [ -f "$candidate" ]; then
        KO="$candidate"
        break
    fi
done
if [ -z "$KO" ]; then
    KO="$(find "$COREWORK" -maxdepth 5 -type f -name thermal_core.ko -print -quit)"
fi
APP="$APPBUILD/thermal_ioctl_test"

if [ -z "$KO" ] || [ ! -s "$KO" ] || [ ! -s "$APP" ]; then
    echo "[ERROR] Missing .ko or app. KO=$KO APP=$APP" >&2
    exit 1
fi
for elf in "$KO" "$APP"; do
    if ! readelf -h "$elf" | grep -q 'Machine:.*AArch64'; then
        echo "[ERROR] Not ARM64: $elf" >&2
        exit 1
    fi
done

# Different filename preserves the 06A transfer artifact for rollback.
cp "$KO" "$SHARE/thermal_core_06c.ko"
cp "$APP" "$SHARE/thermal_ioctl_test"

echo '[PASS] BitBake finished; AArch64 artifacts staged:'
ls -lh "$SHARE/thermal_core_06c.ko" "$SHARE/thermal_ioctl_test"
file "$SHARE/thermal_core_06c.ko" "$SHARE/thermal_ioctl_test"
echo '[NEXT] Run on Pi only after reviewing these results.'
