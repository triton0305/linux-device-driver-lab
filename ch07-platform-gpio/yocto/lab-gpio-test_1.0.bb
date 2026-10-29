SUMMARY = "Chapter 07 GPIO ioctl test and polling application"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
SRC_URI = "file://lab_gpio_test.c file://lab_gpio_uapi.h"
S = "${UNPACKDIR}"
do_compile() {
    ${CC} ${CPPFLAGS} ${CFLAGS} -std=c11 -Wall -Wextra -Werror -I${S} \
        ${S}/lab_gpio_test.c -o ${B}/lab_gpio_test ${LDFLAGS}
}
do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/lab_gpio_test ${D}${bindir}/lab_gpio_test
}
