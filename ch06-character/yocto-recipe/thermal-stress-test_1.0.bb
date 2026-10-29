SUMMARY = "Thermal Character Driver Stress Test"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

SRC_URI = "file://thermal_stress_test.c file://thermal_uapi.h"
S = "${UNPACKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${CPPFLAGS} -std=c11 -O2 -Wall -Wextra -Werror -pthread \
        -I${S} ${S}/thermal_stress_test.c \
        ${LDFLAGS} -o thermal_stress_test
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 thermal_stress_test ${D}${bindir}/thermal_stress_test
}
