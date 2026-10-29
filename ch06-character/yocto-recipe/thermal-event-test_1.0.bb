SUMMARY = "Chapter 06D thermal event character device userspace tests"
LICENSE = "CLOSED"
SRC_URI = "file://thermal_event_test.c file://thermal_uapi.h"
S = "${UNPACKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${CPPFLAGS} -I${S} ${S}/thermal_event_test.c ${LDFLAGS} -o ${B}/thermal_event_test
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/thermal_event_test ${D}${bindir}/thermal_event_test
}
