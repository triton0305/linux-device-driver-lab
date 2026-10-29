SUMMARY = "Chapter 07 Device Tree GPIO character driver"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
SRC_URI = "file://driver/lab_gpio.c file://driver/Makefile file://include/lab_gpio_uapi.h"
S = "${UNPACKDIR}/driver"
inherit module
COMPATIBLE_MACHINE = "^raspberrypi4-64$"
# Deliberately no KERNEL_MODULE_AUTOLOAD and no boot configuration changes.
