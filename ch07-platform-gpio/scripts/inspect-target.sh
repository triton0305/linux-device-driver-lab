#!/bin/sh
# Read-only target evidence. Does not request GPIOs, mount debugfs, or load modules.
printf '\n== Identity ==\n'
uname -a
for f in /proc/device-tree/model /proc/device-tree/compatible; do
    if [ -r "$f" ]; then tr '\000' '\n' < "$f"; fi
done
printf '\n== Boot mounts and command line ==\n'
cat /proc/mounts /proc/cmdline
for f in /boot/config.txt /boot/firmware/config.txt /boot/extlinux/extlinux.conf \
    /boot/firmware/extlinux/extlinux.conf; do
    if [ -r "$f" ]; then printf '\n== %s ==\n' "$f"; cat "$f"; fi
done
printf '\n== GPIO consumers (missing tools are evidence gaps) ==\n'
if command -v gpioinfo >/dev/null 2>&1; then gpioinfo; else echo 'gpioinfo unavailable'; fi
if command -v pinctrl >/dev/null 2>&1; then pinctrl get; else echo 'pinctrl unavailable'; fi
if [ -r /sys/kernel/debug/gpio ]; then cat /sys/kernel/debug/gpio; fi
for f in /sys/kernel/debug/pinctrl/*/pinmux-pins; do
    if [ -r "$f" ]; then cat "$f"; fi
done
printf '\n== Overlay support (no mutation) ==\n'
if command -v dtoverlay >/dev/null 2>&1; then dtoverlay -l; fi
ls -ld /sys/kernel/config/device-tree/overlays 2>/dev/null || :
printf '\n== Platform driver bindings ==\n'
ls -l /sys/bus/platform/drivers/lab-gpio 2>/dev/null || :
printf '\nAudit incomplete until included boot files, live DT, wiring and pin ownership are reviewed.\n'
