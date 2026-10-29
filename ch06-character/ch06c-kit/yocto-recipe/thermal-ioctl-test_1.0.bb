SUMMARY = "Chapter 06C thermal_core ioctl UAPI validation app"
LICENSE = "CLOSED"
SRC_URI = "file://thermal_ioctl_test.c file://thermal_uapi.h"
S = "${UNPACKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} -I${S} ${S}/thermal_ioctl_test.c -o ${B}/thermal_ioctl_test
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/thermal_ioctl_test ${D}${bindir}/thermal_ioctl_test
}
